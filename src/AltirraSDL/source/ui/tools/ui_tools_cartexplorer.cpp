//	AltirraSDL - Cartridge Explorer
//
//	The explorer deliberately uses the emulator's cartridge loader and mapper
// tables.  This keeps .CAR headers, raw images, mapper names, and the data
// shown here in agreement with the cartridge that Altirra would actually run.

#include <stdafx.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include <imgui.h>
#include <SDL3/SDL.h>

#include <vd2/system/VDString.h>
#include <vd2/system/binary.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/checksum.h>
#include <at/atio/cartridgeimage.h>

#include "ui_file_dialog_sdl3.h"
#include "ui_main.h"
#include "ui_explorer_document.h"
#include "ui_atascii.h"
#include "ui_fonts.h"
#include "resource.h"

// Defined in cartdetect.cpp and shared with the mapper-selection dialog.
uint32 ATCartridgeAutodetectMode(const void *data, uint32 size, vdfastvector<int>& cartModes);

// Shared with ui_cartmapper.cpp so the two cartridge tools use one canonical
// set of mapper labels and descriptions.
const char *ATUIGetCartridgeModeName(int mode);
const char *ATUIGetCartridgeModeDesc(int mode);

namespace {

uint32 ComputeByteSum32(const uint8 *data, size_t size) {
	uint32 sum = 0;
	for (size_t i = 0; i < size; ++i)
		sum += data[i];
	return sum;
}

static const char *FormatBytes(uint64 bytes) {
	static char buf[64];
	if (bytes >= 1024 * 1024)
		sprintf(buf, "%.2f MiB", (double)bytes / (1024.0 * 1024.0));
	else if (bytes >= 1024)
		sprintf(buf, "%.1f KiB", (double)bytes / 1024.0);
	else
		sprintf(buf, "%llu bytes", (unsigned long long)bytes);
	return buf;
}

struct CartExplorerState {
	VDStringW path;
	VDStringA status;
	vdfastvector<uint8> fileData;
	vdfastvector<uint8> payloadData;
	vdrefptr<IATCartridgeImage> image;
	vdfastvector<int> recommendedMappers;
	ATCartridgeMode mode = kATCartridgeMode_None;
	uint32 headerMapper = 0;
	uint32 headerChecksum = 0;
	uint32 computedChecksum = 0;
	bool hasHeader = false;
	int bank = 0;

	const uint8 *GetDisplayData() const {
		if (image && image->GetBuffer())
			return (const uint8 *)image->GetBuffer();
		return payloadData.data();
	}

	uint32 GetDisplaySize() const {
		if (image && image->GetBuffer())
			return image->GetImageSize();
		return (uint32)payloadData.size();
	}

	uint32 GetBankSize() const {
		const uint32 size = GetDisplaySize();
		if (!size)
			return 1;
		// The majority of Atari banked cartridges expose an 8 KiB window.
		// Fixed and 5200 images are more useful as one complete bank.
		if (size <= 16 * 1024 || (mode != kATCartridgeMode_None && ATIsCartridge5200Mode(mode)))
			return size;
		return 8 * 1024;
	}

	uint32 GetBankCount() const {
		const uint32 bankSize = GetBankSize();
		return bankSize ? ((GetDisplaySize() + bankSize - 1) / bankSize) : 0;
	}
};

struct CartExplorerDocument : ATUIExplorerDocument {
	CartExplorerState state;
	ATUIExplorerSelection selection;
	ATUIExplorerExport exportJob;
	int view = 0;

	std::string cartExplorerPendingSaveCAR;
	std::string cartExplorerPendingSaveRaw;
};
static std::vector<std::shared_ptr<CartExplorerDocument>> documents;
static CartExplorerDocument *currentDocument = nullptr;
static ATUIExplorerDocument openRequest;
static std::vector<std::string> pendingDocuments;
static void QueueOpen(void *, const char * const *paths, int) {
	if (paths) for (; *paths; ++paths) pendingDocuments.emplace_back(*paths);
}

bool CartExplorerApplyMapper(CartExplorerState &state, ATCartridgeMode mode) {
	if (mode == kATCartridgeMode_None || state.payloadData.empty())
		return false;

	try {
		// Re-run the canonical loader over the payload with the selected
		// mapper.  This preserves the loader's padding and special small-ROM
		// handling, and makes the data used by the explorer identical to the
		// data used when attaching the cartridge.
		VDMemoryStream stream(state.payloadData.data(), (uint32)state.payloadData.size());
		ATCartLoadContext ctx;
		ctx.mbIgnoreChecksum = true;
		ctx.mCartMapper = (int)mode;
		IATCartridgeImage *imageRaw = nullptr;
		if (!ATLoadCartridgeImage(state.path.c_str(), stream, &ctx, &imageRaw) || !imageRaw)
			return false;

		state.image = imageRaw;
		imageRaw->Release();
		state.mode = state.image->GetMode();
		state.bank = 0;
		return true;
	} catch (...) {
		return false;
	}
}

void CartExplorerSaveCARCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	currentDocument->cartExplorerPendingSaveCAR = filelist[0];
}

void CartExplorerSaveRawCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	currentDocument->cartExplorerPendingSaveRaw = filelist[0];
}

void CartExplorerDoOpen(const char *utf8path) {
	const std::string source = ATUIExplorerFullPath(utf8path);
	for (auto& doc : documents) {
		if (doc->open && doc->source == source) { doc->focus = true; return; }
	}
	auto document = std::make_shared<CartExplorerDocument>();
	document->source = source;
	documents.push_back(document);
	ATUIExplorerScope<CartExplorerDocument> scope(currentDocument, document.get());
	try {
		VDStringW path = VDTextU8ToW(utf8path, -1);
		VDFile file(path.c_str());
		const sint64 fileSize = file.size();
		if (fileSize < 1024 || fileSize > 128 * 1024 * 1024 + 16)
			throw MyError("Unsupported cartridge size (must be 1 KiB to 128 MiB).");

		CartExplorerState next;
		next.path = path;
		next.fileData.resize((uint32)fileSize);
		file.read(next.fileData.data(), (long)next.fileData.size());
		file.closeNT();

		const uint8 *source = next.fileData.data();
		uint32 sourceSize = (uint32)next.fileData.size();
		if (sourceSize >= 16 && !memcmp(source, "CART", 4)) {
			next.hasHeader = true;
			next.headerMapper = VDReadUnalignedBEU32(source + 4);
			next.headerChecksum = VDReadUnalignedBEU32(source + 8);
			source += 16;
			sourceSize -= 16;
		}
		next.payloadData.assign(source, source + sourceSize);
		next.computedChecksum = ComputeByteSum32(source, sourceSize);

		// Ignore a bad checksum here only so the explorer can explain it.  The
		// normal cartridge attach path continues to enforce its usual checks.
		VDFileStream stream(path.c_str());
		ATCartLoadContext ctx;
		ctx.mbIgnoreChecksum = true;
		ctx.mbIgnoreMapper = true;
		IATCartridgeImage *imageRaw = nullptr;
		if (ATLoadCartridgeImage(path.c_str(), stream, &ctx, &imageRaw)) {
			next.image = imageRaw;
			if (imageRaw)
				imageRaw->Release();
			next.mode = next.image->GetMode();
		} else {
			// Unknown raw sizes/mappers are still useful to inspect.  The
			// suggestions match the mapper dialog used by cartridge attach.
			ATCartridgeAutodetectMode(next.payloadData.data(), sourceSize, next.recommendedMappers);
		}

		if (!next.payloadData.empty() && next.recommendedMappers.empty()) {
			ATCartridgeAutodetectMode(next.payloadData.data(), sourceSize, next.recommendedMappers);
		}
		if (!next.recommendedMappers.empty() && next.mode == kATCartridgeMode_None)
			CartExplorerApplyMapper(next, (ATCartridgeMode)next.recommendedMappers[0]);

		currentDocument->state = std::move(next);
		currentDocument->state.status = "Loaded cartridge image.";
	} catch (const MyError &e) {
		currentDocument->state.status.sprintf("Open failed: %s", e.c_str());
	} catch (...) {
		currentDocument->state.status = "Open failed: unexpected error.";
	}
}

void CartExplorerDoSaveCAR(const char *utf8path) {
	if (!currentDocument->state.image || currentDocument->state.mode == kATCartridgeMode_None) {
		currentDocument->state.status = "This image has no supported .CAR mapper.";
		return;
	}
	try {
		VDStringW path = VDTextU8ToW(utf8path, -1);
		ATSaveCartridgeImage(currentDocument->state.image, path.c_str(), true);
		currentDocument->state.status = "Saved .CAR image.";
	} catch (const MyError &e) {
		currentDocument->state.status.sprintf("Save failed: %s", e.c_str());
	}
}

void CartExplorerDoSaveRaw(const char *utf8path) {
	try {
		VDStringW path = VDTextU8ToW(utf8path, -1);
		if (currentDocument->state.image) {
			// Let the canonical saver undo any 2K/4K padding applied by the
			// loader.  This is important when the user is round-tripping a
			// small cartridge through the explorer.
			ATSaveCartridgeImage(currentDocument->state.image, path.c_str(), false);
		} else {
			VDFile file(path.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
			const uint8 *data = currentDocument->state.GetDisplayData();
			file.write(data, (long)currentDocument->state.GetDisplaySize());
		}
		currentDocument->state.status = "Saved raw image.";
	} catch (const MyError &e) {
		currentDocument->state.status.sprintf("Save failed: %s", e.c_str());
	}
}

} // namespace

bool ATUICartridgeExplorerGetDropRect(ImVec2 &pos, ImVec2 &size, float x, float y) {
	for (const auto& doc : documents)
		if (doc->Hit(x, y, &pos, &size)) return true;
	return false;
}

bool ATUICartridgeExplorerHandleDrop(const char *utf8path, float dropX, float dropY) {
	if (!utf8path) return false;
	std::shared_ptr<CartExplorerDocument> target;
	for (const auto& doc : documents) if (doc->Hit(dropX, dropY)) target = doc;
	if (!target) return false;
	if (target->dialogPending) return true;
	ATUIExplorerScope<CartExplorerDocument> scope(currentDocument, target.get());

	// Consume every file dropped onto this window, including unsupported files,
	// so they are reported here instead of being mistaken for boot media.
	CartExplorerDoOpen(utf8path);
	return true;
}

static void ExportRegions(SDL_Window *window) {
	auto& doc = *currentDocument;
	doc.exportJob.files.clear();
	const uint32 regionSize = doc.state.GetBankSize();
	for (uint32 i = 0; i < doc.state.GetBankCount(); ++i) {
		if (!doc.selection.items[i]) continue;
		const uint32 offset = i * regionSize;
		const uint32 size = std::min(regionSize, doc.state.GetDisplaySize() - offset);
		char name[80]; snprintf(name, sizeof name, "region-%03u-%06X.bin", i, offset);
		const uint8 *bytes = doc.state.GetDisplayData() + offset;
		doc.exportJob.files.push_back({name, {bytes, bytes + size}});
	}
	doc.exportJob.Start(doc, window);
}
static void RegionActions(SDL_Window *window) {
	if (ImGui::MenuItem("Export selected bytes...")) ExportRegions(window);
	if (ImGui::MenuItem("Copy details")) {
		VDStringA text;
		const auto& state = currentDocument->state;
		for (uint32 i = 0; i < state.GetBankCount(); ++i) {
			if (!currentDocument->selection.items[i]) continue;
			const uint32 offset = i * state.GetBankSize();
			text.append_sprintf("Region %u\t$%06X\t%u bytes\n", i, offset,
				std::min(state.GetBankSize(), state.GetDisplaySize() - offset));
		}
		ImGui::SetClipboardText(text.c_str());
	}
}
static void RenderDocument(ATUIState&, SDL_Window *window) {
	auto& doc = *currentDocument;
	auto& data = doc.state;
	std::string saveCARPath, saveRawPath;
	saveCARPath.swap(doc.cartExplorerPendingSaveCAR);
	saveRawPath.swap(doc.cartExplorerPendingSaveRaw);
	if (!saveCARPath.empty()) CartExplorerDoSaveCAR(saveCARPath.c_str());
	if (!saveRawPath.empty()) CartExplorerDoSaveRaw(saveRawPath.c_str());
	doc.Prepare();
	if (!ImGui::Begin(doc.Title("Cartridge Explorer").c_str(), &doc.open, ImGuiWindowFlags_MenuBar)) {
		doc.Track(); ImGui::End(); return;
	}
	doc.Track();
	if (ATUICheckEscClose()) doc.open = false;
	doc.selection.Resize(data.GetBankCount());
	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open another...")) ATUIRequestCartridgeExplorer(window);
			const bool canSaveCAR = data.image && ATGetCartridgeMapperForMode(data.mode, data.GetDisplaySize()) != 0;
			if (ImGui::MenuItem("Save as .CAR...", nullptr, false, canSaveCAR)) {
				static const SDL_DialogFileFilter filters[] = {{"Atari CAR image", "car"}};
				ATUIExplorerFileDialog(doc, true, 'cart', CartExplorerSaveCARCallback, nullptr, window, filters, 1);
			}
			if (ImGui::MenuItem("Save raw image...", nullptr, false, data.GetDisplaySize() != 0)) {
				static const SDL_DialogFileFilter filters[] = {{"Binary image", "bin;rom"}, {"All files", "*"}};
				ATUIExplorerFileDialog(doc, true, 'cart', CartExplorerSaveRawCallback, nullptr, window, filters, 2);
			}
			if (ImGui::MenuItem("Close")) doc.open = false;
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Selection", doc.selection.Count() > 0)) { RegionActions(window); ImGui::EndMenu(); }
		ImGui::EndMenuBar();
	}
	ATUIExplorerSource(doc.source);
	if (data.payloadData.empty()) {
		ImGui::TextWrapped("%s", data.status.c_str());
		if (ImGui::Button("Open another...")) ATUIRequestCartridgeExplorer(window);
		ImGui::End(); return;
	}
	ImGui::Text("%s   %s", FormatBytes(data.fileData.size()), ATUIGetCartridgeModeName(data.mode));
	if (data.hasHeader && data.headerChecksum != data.computedChecksum)
		ImGui::TextWrapped("CAR checksum mismatch: header $%08X, calculated $%08X.", data.headerChecksum, data.computedChecksum);
		if (ImGui::CollapsingHeader("Image properties and mapper")) {
			if (ImGui::BeginTable("CartInfo", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
				ImVec2(0, 0))) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				auto row = [](const char *name, const char *value) {
					ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
					ImGui::TableNextColumn(); ImGui::TextWrapped("%s", value);
				};
				row("File size", FormatBytes(currentDocument->state.fileData.size()));
				row("Payload size", FormatBytes(currentDocument->state.payloadData.size()));
				row("Interpreted image size", FormatBytes(currentDocument->state.GetDisplaySize()));
				row("Format", currentDocument->state.hasHeader ? "CAR header" : "Raw cartridge image");
				char mapperInfo[256];
				if (currentDocument->state.hasHeader)
					snprintf(mapperInfo, sizeof mapperInfo, "%u", currentDocument->state.headerMapper);
				else strcpy(mapperInfo, "Raw image (no header)");

				row("Header mapper", mapperInfo);
				row("Cartridge type", ATUIGetCartridgeModeName(currentDocument->state.mode));
				row("Bus behavior", ATUIGetCartridgeModeDesc(currentDocument->state.mode));
				row("Target", ATIsCartridge5200Mode(currentDocument->state.mode) ? "Atari 5200" : "Atari 8-bit computer");
				row("Storage region size", FormatBytes(currentDocument->state.GetBankSize()));
				if (currentDocument->state.hasHeader) {
					char checksum[80];
					sprintf(checksum, "$%08X (%s)", currentDocument->state.headerChecksum,
						currentDocument->state.headerChecksum == currentDocument->state.computedChecksum ? "valid" : "mismatch");
					row("CAR checksum", checksum);
				}
				ImGui::EndTable();
			}

			if (!currentDocument->state.recommendedMappers.empty()) {
				ImGui::Separator();
				ImGui::Text("Mapper selection");
				int currentMapper = -1;
				for (int i = 0; i < (int)currentDocument->state.recommendedMappers.size(); ++i) {
					if (currentDocument->state.recommendedMappers[i] == (int)currentDocument->state.mode) {
						currentMapper = i;
						break;
					}
				}
				const char *currentName = ATUIGetCartridgeModeName(currentDocument->state.mode);
				if (ImGui::BeginCombo("Interpret as mapper", currentName)) {
					for (int i = 0; i < (int)currentDocument->state.recommendedMappers.size(); ++i) {
						const int mapper = currentDocument->state.recommendedMappers[i];
						const bool selected = i == currentMapper;
						if (ImGui::Selectable(ATUIGetCartridgeModeName(mapper), selected)) {
							if (CartExplorerApplyMapper(currentDocument->state, (ATCartridgeMode)mapper)) {
								currentDocument->selection.items.clear();
								currentDocument->state.status = "Mapper applied to image.";
							} else currentDocument->state.status = "Unable to apply this mapper.";
						}

						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::TextDisabled("%s", ATUIGetCartridgeModeDesc(currentDocument->state.mode));
			}
		}

	doc.selection.Resize(data.GetBankCount());
	ImGui::BeginDisabled(!doc.selection.Count());
	if (ImGui::Button("Export selected...")) ExportRegions(window);
	ImGui::EndDisabled(); ImGui::SameLine();
	if (ImGui::Button("Select all")) std::fill(doc.selection.items.begin(), doc.selection.items.end(), true);
	ImGui::TextDisabled("Storage regions use image offsets, not mapper bank numbers or CPU addresses.");
	const float total = ImGui::GetContentRegionAvail().y;
	if (ImGui::BeginTable("Contents", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV, ImVec2(0, ATUIExplorerListHeight(doc)))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Storage region"); ImGui::TableSetupColumn("Image offset"); ImGui::TableSetupColumn("Bytes");
		ImGui::TableHeadersRow();
		ImGuiListClipper clipper; clipper.Begin(data.GetBankCount());
		while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
			const uint32 offset = i * data.GetBankSize();
			ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
			const std::string label = "Region " + std::to_string(i);
			if (ImGui::Selectable(label.c_str(), doc.selection.items[i], ImGuiSelectableFlags_SpanAllColumns)) {
				doc.selection.Select(i); data.bank = i;
			}
			if (ImGui::GetCurrentContext()->NavJustMovedToId == ImGui::GetItemID()) {
				doc.selection.Select(i); data.bank = i;
			}
			if (ImGui::BeginPopupContextItem()) {
				doc.selection.Select(i, true); data.bank = i;
				RegionActions(window); ImGui::EndPopup();
			}
			ImGui::TableNextColumn(); ImGui::PushFont(ATUIGetFontMono()); ATUIExplorerNumber("$%06X", offset);
			ImGui::TableNextColumn(); ATUIExplorerNumber("%u", std::min(data.GetBankSize(), data.GetDisplaySize() - offset)); ImGui::PopFont();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ATUIExplorerSplitter(doc, total);
	const uint32 offset = data.bank * data.GetBankSize();
	const uint32 size = std::min(data.GetBankSize(), data.GetDisplaySize() - offset);
	const uint8 *bytes = data.GetDisplayData() + offset;
	ImGui::Text("Region %d   Image offset $%06X   %u bytes   %d selected", data.bank, offset, size, doc.selection.Count());
	ImGui::TextUnformatted("Preview"); ImGui::SameLine();
	ImGui::SetNextItemWidth(210);
	ImGui::Combo("##PreviewMode", &doc.view, "Hex dump\0ATASCII\0ASCII (7-bit)\0");
	if (doc.view) { ImGui::SameLine(); ATUIRenderTextColumnControls("Columns"); }
	if (!doc.exportJob.status.empty()) ImGui::TextWrapped("%s", doc.exportJob.status.c_str());
	else if (!data.status.empty()) ImGui::TextWrapped("%s", data.status.c_str());
	ImGui::BeginChild("Preview", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	if (doc.view == 0) ATUIRenderHexDump(bytes, size, offset);
	else if (doc.view == 1) ATUIRenderATASCII(bytes, size, ATUIGetTextColumns());
	else ATUIRenderASCII(bytes, size, ATUIGetTextColumns());
	ImGui::EndChild(); ImGui::End();
}

void ATUIRequestCartridgeExplorer(SDL_Window *window) {
	static const SDL_DialogFileFilter filters[] = { { "Cartridge images", "car;a52;rom;bin" }, { "All files", "*" } };
	ATUIExplorerFileDialog(openRequest, false, 'cart', QueueOpen, nullptr, window, filters, 2, true);
}

void ATUIRenderCartridgeExplorer(ATUIState &state, SDL_Window *window) {
	openRequest.Drain();
	std::vector<std::string> paths;
	paths.swap(pendingDocuments);
	for (const auto& path : paths) CartExplorerDoOpen(path.c_str());
	// Snapshot: opening another document while drawing must not invalidate iteration.
	const auto snapshot = documents;
	for (const auto& doc : snapshot) {
		if (!doc->open) continue;
		ATUIExplorerScope<CartExplorerDocument> scope(currentDocument, doc.get());
		doc->Drain();
		ImGui::BeginDisabled(doc->dialogPending);
		RenderDocument(state, window);
		ImGui::EndDisabled();
		if (doc->requestClose) { doc->requestClose = false; doc->open = false; }
	}
	documents.erase(std::remove_if(documents.begin(), documents.end(),
		[](const auto& doc) { return !doc->open; }), documents.end());
	state.showCartridgeExplorer = !documents.empty() || openRequest.dialogPending;
}

void ATUIOpenCartridgeExplorerFile(const char *path) {
	if (path && *path) pendingDocuments.emplace_back(path);
}

void ATUICloseCartridgeExplorers() {
	for (const auto& doc : documents) doc->requestClose = true;
}
void ATUIShutdownCartridgeExplorers() { documents.clear(); }
