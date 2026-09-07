//	AltirraSDL - Atari executable (XEX) Explorer

#include <stdafx.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/VDString.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>

#include "ui_file_dialog_sdl3.h"
#include "ui_main.h"
#include "ui_explorer_document.h"
#include "ui_atascii.h"
#include "ui_diskexplorer_views.h"
#include "ui_fonts.h"

namespace {

struct XEXSegment {
	uint16 start = 0;
	uint16 end = 0;
	size_t dataOffset = 0;
	std::string notes;

	uint32 Size() const { return (uint32)end - start + 1; }
};

struct XEXExplorerState {
	VDStringW path;
	VDStringA status;
	vdfastvector<uint8> data;
	std::vector<XEXSegment> segments;
	int selectedSegment = 0;
};

struct XEXExplorerDocument : ATUIExplorerDocument {
	XEXExplorerState state;
	ATUIExplorerSelection selection;
	ATUIExplorerExport exportJob;
	int view = 0;
	int decodedSegment = -1;
	VDStringA disassembly, decodeError;
};
static std::vector<std::shared_ptr<XEXExplorerDocument>> documents;
static XEXExplorerDocument *currentDocument = nullptr;
static ATUIExplorerDocument openRequest;
static std::vector<std::string> pendingDocuments;
static void QueueOpen(void *, const char * const *paths, int) {
	if (paths) for (; *paths; ++paths) pendingDocuments.emplace_back(*paths);
}

void XEXExplorerDoOpen(const char *utf8path, const uint8 *buffer = nullptr, size_t length = 0) {
	const std::string source = buffer ? utf8path : ATUIExplorerFullPath(utf8path);
	for (auto& doc : documents) {
		if (doc->open && doc->source == source) { doc->focus = true; return; }
	}
	auto document = std::make_shared<XEXExplorerDocument>();
	document->source = source;
	documents.push_back(document);
	ATUIExplorerScope<XEXExplorerDocument> scope(currentDocument, document.get());
	try {
		XEXExplorerState next;
		next.path = VDTextU8ToW(utf8path, -1);

		if (buffer) next.data.assign(buffer, buffer + length);
		else {
			VDFile file(next.path.c_str());
			const sint64 fileSize = file.size();
			if (fileSize < 4 || fileSize > 64 * 1024 * 1024)
				throw MyError("Unsupported executable size.");
			next.data.resize((size_t)fileSize);
			file.read(next.data.data(), (long)next.data.size());
		}
		std::vector<bool> loaded(65536, false);

		std::vector<ATUIXEXSegmentInfo> parsedSegments;
		VDStringA parseError;
		if (!ATUIParseXEX(next.data.data(), next.data.size(), parsedSegments, parseError))
			throw MyError("%s", parseError.c_str());
		for (const auto& parsed : parsedSegments) {
			const uint16 start = (uint16)parsed.start;
			const uint16 end = (uint16)parsed.end;
			XEXSegment segment{start, end, parsed.dataOffset, {}};
			// RUNAD/INITAD encodings match the existing executable viewer in
			// DiskExplorerState::FormatView. Include partial vector writes.
			if (start <= 0x02E1 && end >= 0x02E0) segment.notes += "Writes RUN vector";
			if (start <= 0x02E3 && end >= 0x02E2) {
				if (!segment.notes.empty()) segment.notes += "; ";
				segment.notes += "Writes INIT vector";
			}
			bool overlap = false;
			for (uint32 address = start; address <= end; ++address) {
				overlap |= loaded[address]; loaded[address] = true;
			}
			if (overlap) {
				if (!segment.notes.empty()) segment.notes += "; ";
				segment.notes += "Overwrites earlier data";
			}
			next.segments.push_back(std::move(segment));
		}
		next.status = "Loaded Atari executable.";
		currentDocument->state = std::move(next);
	} catch (const MyError& e) {
		currentDocument->state.status.sprintf("Open failed: %s", e.c_str());
	} catch (...) {
		currentDocument->state.status = "Open failed: unexpected error.";
	}
}

} // namespace

bool ATUIXEXExplorerGetDropRect(ImVec2 &pos, ImVec2 &size, float x, float y) {
	for (const auto& doc : documents)
		if (doc->Hit(x, y, &pos, &size)) return true;
	return false;
}

bool ATUIXEXExplorerHandleDrop(const char *utf8path, float dropX, float dropY) {
	if (!utf8path) return false;
	std::shared_ptr<XEXExplorerDocument> target;
	for (const auto& doc : documents) if (doc->Hit(dropX, dropY)) target = doc;
	if (!target) return false;
	if (target->dialogPending) return true;
	ATUIExplorerScope<XEXExplorerDocument> scope(currentDocument, target.get());

	// Consume unsupported files as well: the XEX Explorer should report a
	// format error instead of allowing a drop on its window to boot the file.
	XEXExplorerDoOpen(utf8path);
	return true;
}

static void ExportSegments(SDL_Window *window, bool executable) {
	auto& doc = *currentDocument;
	doc.exportJob.files.clear();
	if (executable) doc.exportJob.files.push_back({"segments.xex", {0xFF, 0xFF}});
	for (size_t i = 0; i < doc.state.segments.size(); ++i) {
		if (!doc.selection.items[i]) continue;
		const auto& segment = doc.state.segments[i];
		const uint8 *data = doc.state.data.data() + segment.dataOffset;
		if (executable) {
			auto& out = doc.exportJob.files.front().bytes;
			out.insert(out.end(), {uint8(segment.start), uint8(segment.start >> 8),
				uint8(segment.end), uint8(segment.end >> 8)});
			out.insert(out.end(), data, data + segment.Size());
		} else {
			char name[80];
			snprintf(name, sizeof name, "segment-%03u-%04X-%04X.bin", unsigned(i + 1), segment.start, segment.end);
			doc.exportJob.files.push_back({name, {data, data + segment.Size()}});
		}
	}
	doc.exportJob.Start(doc, window);
}

static void SegmentActions(SDL_Window *window) {
	if (ImGui::MenuItem("Export selected bytes...")) ExportSegments(window, false);
	if (ImGui::MenuItem("Export selected as XEX...")) ExportSegments(window, true);
	if (ImGui::MenuItem("Copy details")) {
		VDStringA text;
		for (size_t i = 0; i < currentDocument->state.segments.size(); ++i) {
			if (!currentDocument->selection.items[i]) continue;
			const auto& segment = currentDocument->state.segments[i];
			text.append_sprintf("%u\t$%04X\t$%04X\t%u\t%s\n", unsigned(i + 1),
				segment.start, segment.end, segment.Size(), segment.notes.c_str());
		}
		ImGui::SetClipboardText(text.c_str());
	}
}

static void RenderDocument(ATUIState&, SDL_Window *window) {
	auto& doc = *currentDocument;
	auto& data = doc.state;
	doc.Prepare();
	if (!ImGui::Begin(doc.Title("XEX Explorer").c_str(), &doc.open, ImGuiWindowFlags_MenuBar)) {
		doc.Track(); ImGui::End(); return;
	}
	doc.Track();
	if (ATUICheckEscClose()) doc.open = false;
	doc.selection.Resize((int)data.segments.size());
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open another...")) ATUIRequestXEXExplorer(window);
			if (ImGui::MenuItem("Close")) doc.open = false;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Selection", doc.selection.Count() > 0)) {
			SegmentActions(window); ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	ATUIExplorerSource(doc.source);
	if (data.segments.empty()) {
		ImGui::TextWrapped("%s", data.status.c_str());
		if (ImGui::Button("Open another...")) ATUIRequestXEXExplorer(window);
		ImGui::End(); return;
	}
	ImGui::Text("%u segments   %u bytes", unsigned(data.segments.size()), unsigned(data.data.size()));
	ImGui::SameLine();
	ImGui::BeginDisabled(!doc.selection.Count());
	if (ImGui::Button("Export selected...")) ImGui::OpenPopup("ExportSelection");
	if (ImGui::BeginPopup("ExportSelection")) { SegmentActions(window); ImGui::EndPopup(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Select all")) std::fill(doc.selection.items.begin(), doc.selection.items.end(), true);
	ImGui::TextDisabled("Load order is preserved. Exporting a subset as XEX may not produce a runnable program.");
	const float total = ImGui::GetContentRegionAvail().y;
	if (ImGui::BeginTable("Contents", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV, ImVec2(0, ATUIExplorerListHeight(doc)))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Load order"); ImGui::TableSetupColumn("Start");
		ImGui::TableSetupColumn("End"); ImGui::TableSetupColumn("Bytes");
		ImGui::TableSetupColumn("Role / overlap", ImGuiTableColumnFlags_WidthStretch, 2);
		ImGui::TableHeadersRow();
		ImGuiListClipper clipper; clipper.Begin((int)data.segments.size());
		while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
			const auto& segment = data.segments[i];
			ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
			const std::string label = "Segment " + std::to_string(i + 1);
			if (ImGui::Selectable(label.c_str(), doc.selection.items[i], ImGuiSelectableFlags_SpanAllColumns)) {
				doc.selection.Select(i); data.selectedSegment = i;
			}
			if (ImGui::GetCurrentContext()->NavJustMovedToId == ImGui::GetItemID()) {
				doc.selection.Select(i); data.selectedSegment = i;
			}
			if (ImGui::BeginPopupContextItem()) {
				doc.selection.Select(i, true); data.selectedSegment = i;
				SegmentActions(window); ImGui::EndPopup();
			}
			ImGui::TableNextColumn(); ImGui::PushFont(ATUIGetFontMono()); ATUIExplorerNumber("$%04X", segment.start);
			ImGui::TableNextColumn(); ATUIExplorerNumber("$%04X", segment.end);
			ImGui::TableNextColumn(); ATUIExplorerNumber("%u", segment.Size()); ImGui::PopFont();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(segment.notes.c_str());
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ATUIExplorerSplitter(doc, total);
	const auto& segment = data.segments[data.selectedSegment];
	const uint8 *bytes = data.data.data() + segment.dataOffset;
	ImGui::Text("Segment %d   $%04X-$%04X   %u bytes   %d selected", data.selectedSegment + 1,
		segment.start, segment.end, segment.Size(), doc.selection.Count());
	for (const auto& vector : {std::pair<unsigned, const char *>{0x02E0, "RUN"}, {0x02E2, "INIT"}}) {
		if (segment.start <= vector.first && segment.end >= vector.first + 1) {
			const auto offset = vector.first - segment.start;
			ImGui::SameLine(); ImGui::Text("%s: $%04X", vector.second, bytes[offset] | (bytes[offset + 1] << 8));
		}
	}
	ImGui::TextUnformatted("Preview"); ImGui::SameLine();
	ImGui::SetNextItemWidth(210);
	ImGui::Combo("##PreviewMode", &doc.view, "Hex dump\0ATASCII\0ASCII (7-bit)\0" "6502 disassembly\0");
	if (doc.view == 1 || doc.view == 2) { ImGui::SameLine(); ATUIRenderTextColumnControls("Columns"); }
	if (!doc.exportJob.status.empty()) ImGui::TextWrapped("%s", doc.exportJob.status.c_str());
	ImGui::BeginChild("Preview", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	if (doc.view == 0) ATUIRenderHexDump(bytes, segment.Size(), segment.start);
	else if (doc.view == 1) ATUIRenderATASCII(bytes, segment.Size(), ATUIGetTextColumns());
	else if (doc.view == 2) ATUIRenderASCII(bytes, segment.Size(), ATUIGetTextColumns());
	else {
		if (doc.decodedSegment != data.selectedSegment) {
			ATUIDisassemble6502(bytes, segment.Size(), segment.start, doc.disassembly, doc.decodeError);
			doc.decodedSegment = data.selectedSegment;
		}
		ImGui::PushFont(ATUIGetFontMono());
		ImGui::TextUnformatted(doc.disassembly.empty() ? doc.decodeError.c_str() : doc.disassembly.c_str());
		ImGui::PopFont();
	}
	ImGui::EndChild(); ImGui::End();
}

void ATUIOpenXEXExplorerData(const char *origin, const uint8 *data, size_t size) {
	if (data && size) XEXExplorerDoOpen(origin, data, size);
}

void ATUIRequestXEXExplorer(SDL_Window *window) {
	static const SDL_DialogFileFilter filters[] = { { "Atari executables", "xex;obx;com;exe" }, { "All files", "*" } };
	ATUIExplorerFileDialog(openRequest, false, 'xex ', QueueOpen, nullptr, window, filters, 2, true);
}

void ATUIRenderXEXExplorer(ATUIState &state, SDL_Window *window) {
	openRequest.Drain();
	std::vector<std::string> paths;
	paths.swap(pendingDocuments);
	for (const auto& path : paths) XEXExplorerDoOpen(path.c_str());
	// Snapshot: opening another document while drawing must not invalidate iteration.
	const auto snapshot = documents;
	for (const auto& doc : snapshot) {
		if (!doc->open) continue;
		ATUIExplorerScope<XEXExplorerDocument> scope(currentDocument, doc.get());
		doc->Drain();
		ImGui::BeginDisabled(doc->dialogPending);
		RenderDocument(state, window);
		ImGui::EndDisabled();
		if (doc->requestClose) { doc->requestClose = false; doc->open = false; }
	}
	documents.erase(std::remove_if(documents.begin(), documents.end(),
		[](const auto& doc) { return !doc->open; }), documents.end());
	state.showXEXExplorer = !documents.empty() || openRequest.dialogPending;
}

void ATUIOpenXEXExplorerFile(const char *path) {
	if (path && *path) pendingDocuments.emplace_back(path);
}

void ATUICloseXEXExplorers() {
	for (const auto& doc : documents) doc->requestClose = true;
}
void ATUIShutdownXEXExplorers() { documents.clear(); }
