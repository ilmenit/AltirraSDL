//	AltirraSDL - Help → Contents browser window.
//
//	Tool window with TOC sidebar + topic content pane.  Decompresses the
//	baked help blob on first open and walks the topic-block stream each
//	frame.  Hyperlinks navigate within the help corpus; external URLs go
//	through ATLaunchURL.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include "ui_main.h"
#include "ui_main_internal.h"
#include "ui_help_contents.h"

// Cross-platform URL launcher (defined in os/oshelper_sdl3.cpp).
extern void ATLaunchURL(const wchar_t *url);

// ESC close helper used by other tool dialogs.
extern bool ATUICheckEscClose();

namespace {

struct HistoryEntry {
	int  topicIndex;
	float scrollY;
	std::string anchor;       // empty if none
};

constexpr size_t kMaxHistory = 32;

std::vector<HistoryEntry> s_history;
size_t s_histIndex = 0;          // index of current entry; entries
                                 // [0..s_histIndex] are "behind",
                                 // s_histIndex itself is current.
bool s_initialized = false;

// Pending navigation request — applied at the start of the next frame.
int  s_pendingTopic = -1;
std::string s_pendingAnchor;
bool s_pushOnNavigate = true;     // false during back/forward

// Splitter state.
float s_sidebarWidth = 280.0f;
// Narrow windows show either contents or the article at full width.
bool s_showContents = false;

// Find state.
char s_findBuf[128] = {0};
std::string s_lastFind;
int s_currentMatchIdx = -1;       // index into ctx.matchBlocks (frame-local)
int s_currentMatchBlock = -1;     // block index of the highlighted match

// Pending scroll-to-block index (applied after content is laid out).
int  s_pendingScrollBlock = -1;
int  s_pendingScrollFrames = 0;   // small countdown so layout settles

// Currently displayed topic index.
int s_currentTopic = -1;

// TreeNode-open tracker for the path to s_currentTopic — used to
// auto-expand the sidebar tree when navigating.
std::string s_lastSelectedHref;

void DoNavigate(int topicIndex, const std::string &anchor, bool pushHistory)
{
	if (topicIndex < 0) return;
	auto &data = ATHelp::GetHelpData();
	if (topicIndex >= (int)data.TopicCount()) return;

	if (pushHistory && s_currentTopic >= 0) {
		// Truncate forward history.
		if (s_histIndex + 1 < s_history.size())
			s_history.resize(s_histIndex + 1);
		// Cap.
		if (s_history.size() >= kMaxHistory)
			s_history.erase(s_history.begin());
		HistoryEntry e;
		e.topicIndex = topicIndex;
		e.scrollY = 0.0f;
		e.anchor = anchor;
		s_history.push_back(e);
		s_histIndex = s_history.size() - 1;
	}

	s_currentTopic = topicIndex;
	s_showContents = false;
	s_pendingScrollBlock = -1;
	s_pendingScrollFrames = 0;

	if (!anchor.empty()) {
		const auto &topic = data.GetTopic(topicIndex);
		int blk = data.FindAnchor(topic, anchor.c_str());
		if (blk >= 0) {
			s_pendingScrollBlock = blk;
			s_pendingScrollFrames = 2;
		}
	} else {
		// Scroll to top.
		s_pendingScrollBlock = 0;
		s_pendingScrollFrames = 2;
	}

	s_currentMatchIdx = -1;
	s_currentMatchBlock = -1;

	const char *fn = data.GetString(data.GetTopic(topicIndex).filenameIdx);
	s_lastSelectedHref = fn;
}

// Resolve a link string ("foo.html#bar", "#bar", or external URL).
// Updates s_pendingTopic/s_pendingAnchor or opens external.  Returns
// true if it handled the link.
bool HandleLink(const std::string &link)
{
	if (link.empty()) return false;
	// External: starts with "http:" / "https:" / "mailto:".
	auto isExternal = [](const std::string &s) {
		return s.rfind("http://", 0) == 0 ||
		       s.rfind("https://", 0) == 0 ||
		       s.rfind("mailto:", 0) == 0;
	};
	if (isExternal(link)) {
		VDStringW w = VDTextU8ToW(VDStringSpanA(link.c_str()));
		ATLaunchURL(w.c_str());
		return true;
	}
	// Internal __block_<n> marker from MiniTOC click.
	if (link.rfind("#__block_", 0) == 0) {
		int blk = std::atoi(link.c_str() + 9);
		if (blk >= 0) {
			s_pendingScrollBlock = blk;
			s_pendingScrollFrames = 2;
		}
		return true;
	}
	// Same-doc anchor.
	if (link[0] == '#') {
		s_pendingAnchor = link.substr(1);
		s_pendingTopic = s_currentTopic;
		s_pushOnNavigate = true;
		return true;
	}
	// "foo.html" or "foo.html#bar"
	std::string page = link;
	std::string anchor;
	auto h = link.find('#');
	if (h != std::string::npos) {
		page = link.substr(0, h);
		anchor = link.substr(h + 1);
	}
	auto &data = ATHelp::GetHelpData();
	int idx = data.FindTopicByFilename(page.c_str());
	if (idx >= 0) {
		s_pendingTopic = idx;
		s_pendingAnchor = anchor;
		s_pushOnNavigate = true;
		return true;
	}
	return false;
}

void RenderTocNode(uint16_t firstSiblingOff, int depth)
{
	auto &data = ATHelp::GetHelpData();
	const ATHelp::TocNode *nodes = data.TocNodes();
	if (!nodes || firstSiblingOff == ATHelp::kTocNoSibling) return;

	uint16_t cur = firstSiblingOff;
	while (cur != ATHelp::kTocNoSibling) {
		const ATHelp::TocNode *n =
			reinterpret_cast<const ATHelp::TocNode *>(
				reinterpret_cast<const uint8_t *>(nodes) + cur);
		const char *name = data.GetString(n->nameIdx);
		const char *href = data.GetString(n->hrefIdx);
		bool hasChildren = n->hasChildren != 0;
		bool hasHref = href && *href;

		// Match current topic against href.
		bool isCurrent = false;
		if (hasHref && s_currentTopic >= 0) {
			int idx = data.FindTopicByFilename(href);
			isCurrent = (idx == s_currentTopic);
		}

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
		                           ImGuiTreeNodeFlags_OpenOnDoubleClick |
		                           ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf |
			         ImGuiTreeNodeFlags_NoTreePushOnOpen |
			         ImGuiTreeNodeFlags_Bullet;
		if (depth == 0)
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		if (isCurrent)
			flags |= ImGuiTreeNodeFlags_Selected;

		ImGui::PushID((const void *)n);
		bool open = ImGui::TreeNodeEx(name, flags, "%s", name);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && hasHref) {
			s_pendingTopic = data.FindTopicByFilename(href);
			s_pendingAnchor.clear();
			s_pushOnNavigate = true;
		}
		if (open && hasChildren) {
			// Children of a non-leaf node always start at offset+8.
			RenderTocNode((uint16_t)(cur + sizeof(ATHelp::TocNode)),
			              depth + 1);
			ImGui::TreePop();
		}
		ImGui::PopID();

		cur = n->nextSiblingOff;
	}
}

void ToolbarSameLine(const char* label)
{
	const auto& style = ImGui::GetStyle();
	const float used = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
	const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
	if (used + style.ItemSpacing.x + width <= ImGui::GetContentRegionAvail().x)
		ImGui::SameLine();
}

void RenderToolbar(bool compact)
{
	bool canBack = (s_histIndex > 0);
	bool canFwd = (s_histIndex + 1 < s_history.size());

	ImGui::BeginDisabled(!canBack);
	if (ImGui::Button("◀##back") && canBack) {
		--s_histIndex;
		const auto &e = s_history[s_histIndex];
		DoNavigate(e.topicIndex, e.anchor, /*pushHistory=*/false);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back");

	ToolbarSameLine("▶");
	ImGui::BeginDisabled(!canFwd);
	if (ImGui::Button("▶##fwd") && canFwd) {
		++s_histIndex;
		const auto &e = s_history[s_histIndex];
		DoNavigate(e.topicIndex, e.anchor, /*pushHistory=*/false);
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward");

	ToolbarSameLine("Home");
	if (ImGui::Button("Home")) {
		auto &data = ATHelp::GetHelpData();
		int idx = data.FindTopicByFilename("index.html");
		if (idx >= 0) DoNavigate(idx, "", true);
	}

	if (compact) {
		const char* label = s_showContents ? "Back to topic" : "Contents";
		ToolbarSameLine(label);
		if (ImGui::Button(label)) s_showContents = !s_showContents;
	}

	// Keep Find + Next together on a fresh row when navigation uses the
	// available width, including at larger font sizes.
	const auto& style = ImGui::GetStyle();
	const float nextWidth = ImGui::CalcTextSize("Next").x + style.FramePadding.x * 2;
	const float used = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
	if (used + ImGui::GetFontSize() * 17 + nextWidth + style.ItemSpacing.x * 2
		<= ImGui::GetContentRegionAvail().x)
		ImGui::SameLine();
	ImGui::SetNextItemWidth(std::max(1.0f, std::min(ImGui::GetFontSize() * 17,
		ImGui::GetContentRegionAvail().x - nextWidth - style.ItemSpacing.x)));
	bool changed = ImGui::InputTextWithHint("##find", "Find in topic",
		s_findBuf, sizeof(s_findBuf));
	if (changed) {
		s_lastFind = s_findBuf;
		s_currentMatchIdx = -1;
		s_currentMatchBlock = -1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Next") && !s_lastFind.empty()) {
		// Bump current match index; the renderer will refresh
		// matchBlocks each frame, so we cycle when the index falls off
		// the end.
		++s_currentMatchIdx;  // applied next frame via the ctx loop
	}
}

void RenderContentPane()
{
	auto &data = ATHelp::GetHelpData();
	if (s_currentTopic < 0) {
		ImGui::TextDisabled("Select a topic from the table of contents.");
		return;
	}

	const auto &topic = data.GetTopic(s_currentTopic);

	// Title.
	const char *title = data.GetString(topic.titleIdx);
	ImFont *f = ImGui::GetFont();
	float oldScale = f ? f->Scale : 1.0f;
	if (f) {
		f->Scale = oldScale * 1.55f;
		ImGui::PushFont(f);
	}
	ImGui::TextWrapped("%s", title);
	if (f) {
		ImGui::PopFont();
		f->Scale = oldScale;
	}
	ImGui::Separator();
	ImGui::Spacing();

	ATHelp::RenderContext ctx;
	ctx.findText = s_lastFind;
	ctx.currentMatchBlock = s_currentMatchBlock;
	ATHelp::RenderTopic(topic, ctx);

	// Apply pending scroll: the previous frame computed Y positions for
	// each block.  We keep a small countdown to give layout a chance to
	// stabilize.
	if (s_pendingScrollBlock >= 0 &&
	    s_pendingScrollBlock < (int)ctx.blockY.size()) {
		float y = ctx.blockY[s_pendingScrollBlock];
		ImGui::SetScrollY(std::max(0.0f, y - 8.0f));
		if (--s_pendingScrollFrames <= 0)
			s_pendingScrollBlock = -1;
	}

	// Resolve "Next" find click — use this frame's matchBlocks list.
	if (!ctx.matchBlocks.empty() && s_currentMatchIdx >= 0) {
		s_currentMatchIdx %= (int)ctx.matchBlocks.size();
		s_currentMatchBlock = ctx.matchBlocks[s_currentMatchIdx];
		s_pendingScrollBlock = s_currentMatchBlock;
		s_pendingScrollFrames = 2;
		s_currentMatchIdx = -1;       // consumed
	}

	// Handle link click.
	if (!ctx.clickedLink.empty()) {
		HandleLink(ctx.clickedLink);
	}
}

} // namespace

// ===========================================================================
// Public entry points (declared in ui_main_internal.h).
// ===========================================================================

void ATUIHelpShutdown()
{
	ATHelp::ReleaseAllImageTextures();
	s_history.clear();
	s_histIndex = 0;
	s_initialized = false;
	s_currentTopic = -1;
	s_pendingTopic = -1;
	s_pendingAnchor.clear();
	s_pendingScrollBlock = -1;
	s_findBuf[0] = 0;
	s_lastFind.clear();
	s_showContents = false;
}

void ATUIShowHelpTopic(const char *page, const char *anchor)
{
	auto &data = ATHelp::GetHelpData();
	if (!data.EnsureLoaded()) return;
	int idx = data.FindTopicByFilename(page ? page : "index.html");
	if (idx < 0) idx = data.FindTopicByFilename("index.html");
	if (idx < 0) return;
	// The main render loop opens the help pane for pending requests, so
	// contextual help works from both desktop dialogs and Gaming Mode.
	s_pendingTopic = idx;
	s_pendingAnchor = anchor ? anchor : "";
	s_pushOnNavigate = true;
}

bool ATUIHelpHasPendingTopic() { return s_pendingTopic >= 0; }

const char* ATUIGetCurrentHelpTopic() {
	auto& data = ATHelp::GetHelpData();
	return s_currentTopic < 0 || !data.EnsureLoaded() ? ""
		: data.GetString(data.GetTopic(s_currentTopic).filenameIdx);
}

void ATUIRenderHelpContents(ATUIState &state)
{
	if (!state.showHelpContents) return;

	auto &data = ATHelp::GetHelpData();
	if (!data.EnsureLoaded()) {
		// Show an error-only window.
		ImGui::SetNextWindowSize(ImVec2(400, 100), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Help — Contents", &state.showHelpContents)) {
			ImGui::TextWrapped("Help data is unavailable in this build.");
		}
		ImGui::End();
		return;
	}

	// Initialise default topic on first open.
	if (!s_initialized) {
		int idx = data.FindTopicByFilename("index.html");
		if (idx >= 0) DoNavigate(idx, "", false);
		s_history.clear();
		HistoryEntry e;
		e.topicIndex = s_currentTopic;
		e.scrollY = 0.0f;
		s_history.push_back(e);
		s_histIndex = 0;
		s_initialized = true;
	}

	// Apply pending navigation request (set via ATUIShowHelpTopic /
	// link clicks last frame).
	if (s_pendingTopic >= 0) {
		DoNavigate(s_pendingTopic, s_pendingAnchor, s_pushOnNavigate);
		s_pendingTopic = -1;
		s_pendingAnchor.clear();
		s_pushOnNavigate = true;
	}

	ATUIConstrainDialogSize();
	ImGui::SetNextWindowSize(ImVec2(960, 720), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Help — Contents", &state.showHelpContents)) {
		ImGui::End();
		return;
	}
	ATUIClampDialogPosition();
	if (ATUICheckEscClose())
		state.showHelpContents = false;

	const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 52;
	RenderToolbar(compact);
	ImGui::Separator();

	if (compact) {
		if (s_showContents) {
			ImGui::BeginChild("##toc", ImVec2(0, 0), ImGuiChildFlags_Borders);
			RenderTocNode(data.TocFirstRootOff(), 0);
			ImGui::EndChild();
		} else {
			ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_Borders,
				ImGuiWindowFlags_HorizontalScrollbar);
			RenderContentPane();
			ImGui::EndChild();
		}
		ImGui::End();
		return;
	}

	// Two-pane layout via splitter.
	const float availW = ImGui::GetContentRegionAvail().x;
	const float availH = ImGui::GetContentRegionAvail().y;
	if (s_sidebarWidth > availW - 200) s_sidebarWidth = availW - 200;
	if (s_sidebarWidth < 120) s_sidebarWidth = 120;

	ImGui::BeginChild("##toc",
		ImVec2(s_sidebarWidth, availH),
		ImGuiChildFlags_Borders);
	{
		RenderTocNode(data.TocFirstRootOff(), 0);
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Splitter handle.
	ImGui::Button("##split", ImVec2(6.0f, availH));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (ImGui::IsItemActive())
		s_sidebarWidth += ImGui::GetIO().MouseDelta.x;
	ImGui::SameLine();

	ImGui::BeginChild("##content",
		ImVec2(0, availH),
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar);
	{
		RenderContentPane();
	}
	ImGui::EndChild();

	ImGui::End();
}
