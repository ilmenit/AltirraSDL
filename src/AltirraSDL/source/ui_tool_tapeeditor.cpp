//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
//	SDL3/ImGui tape editor — main window, menu bar, toolbar, status bar
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <mutex>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <at/atio/cassetteimage.h>
#include "cassette.h"
#include "simulator.h"
#include "ui_main.h"
#include "ui_tool_tapeeditor.h"
#include "logging.h"

extern ATSimulator g_sim;

// ---- Persistent state ----
static ATTapeEditorState g_tapeEditor;
static bool g_tapeEditorInitialized = false;

// ---- File dialog results (thread-safe queue) ----
static std::mutex g_tapeEditorMutex;

enum class TapeFileAction {
	None,
	Open,
	SaveCAS,
	SaveWAV,
	ExtractCFile,
};

static TapeFileAction g_tapeFileAction = TapeFileAction::None;
static std::string g_tapeFilePath;

static void TapeEditorFileCallback(void *userdata, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	std::lock_guard<std::mutex> lock(g_tapeEditorMutex);
	g_tapeFileAction = (TapeFileAction)(uintptr_t)userdata;
	g_tapeFilePath = filelist[0];
}

// ---- Initialize / sync state ----

static void TapeEditorInit() {
	if (g_tapeEditorInitialized)
		return;

	g_tapeEditorInitialized = true;
	g_tapeEditor.InitSpectrogram();

	// Load saved settings
	VDRegistryAppKey key("Settings", false);
	VDStringW s;
	key.getString("Tape Editor: Waveform Mode", s);
	if (s == L"waveform")
		g_tapeEditor.mWaveformMode = TapeWaveformMode::Waveform;
	else if (s == L"spectrogram")
		g_tapeEditor.mWaveformMode = TapeWaveformMode::Spectrogram;
	else if (s == L"none")
		g_tapeEditor.mWaveformMode = TapeWaveformMode::None;

	g_tapeEditor.mbShowFrequencyGuidelines = key.getBool("Tape Editor: Show Frequency Guidelnes", false);
}

static void TapeEditorSyncImage() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	IATCassetteImage *img = cas.GetImage();

	if (g_tapeEditor.mpImage != img)
		g_tapeEditor.SetImage(img);

	// Update head position
	if (cas.IsLoaded()) {
		g_tapeEditor.mHeadPosition = cas.GetSamplePos();
		g_tapeEditor.mbHeadPlay = cas.IsPlayEnabled();
		g_tapeEditor.mbHeadRecord = cas.IsRecordEnabled();
	}
}

static void TapeEditorSaveSettings() {
	VDRegistryAppKey key("Settings", true);

	const char *modeStr = "waveform";
	switch (g_tapeEditor.mWaveformMode) {
		case TapeWaveformMode::None: modeStr = "none"; break;
		case TapeWaveformMode::Waveform: modeStr = "waveform"; break;
		case TapeWaveformMode::Spectrogram: modeStr = "spectrogram"; break;
	}
	key.setString("Tape Editor: Waveform Mode", VDTextU8ToW(VDStringSpanA(modeStr)).c_str());
	key.setBool("Tape Editor: Show Frequency Guidelnes", g_tapeEditor.mbShowFrequencyGuidelines);
}

// ---- Dirty check ----

static bool g_tapeDiscardPending = false;
static enum class TapePendingAction { None, New, Open, Reload } g_tapePendingAction = TapePendingAction::None;

static bool TapeEditorOKToDiscard() {
	return !g_sim.GetCassette().IsImageDirty();
}

// ---- File operations ----

static void TapeEditorNew() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	cas.LoadNew();
	g_tapeEditor.SetImage(cas.GetImage());
}

static void TapeEditorLoad(const char *utf8path) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	cas.Unload();

	VDStringW wpath = VDTextU8ToW(VDStringSpanA(utf8path));

	ATCassetteLoadContext ctx;
	cas.GetLoadOptions(ctx);
	ctx.mbStoreWaveform = g_tapeEditor.mbStoreWaveformOnLoad;

	vdrefptr<IATCassetteImage> image = ATLoadCassetteImage(wpath.c_str(), nullptr, ctx);
	cas.Load(image, wpath.c_str(), true);

	g_tapeEditor.SetImage(cas.GetImage());
}

static void TapeEditorReload() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (!cas.GetImage() || !cas.IsImagePersistent())
		return;

	VDStringW path(cas.GetPath());

	g_tapeEditor.mViewResetLock++;
	try {
		TapeEditorLoad(VDTextWToU8(VDStringSpanW(path)).c_str());
	} catch (...) {
		g_tapeEditor.mViewResetLock--;
		throw;
	}
	g_tapeEditor.mViewResetLock--;
}

static void TapeEditorSaveCAS(const char *utf8path) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	IATCassetteImage *image = cas.GetImage();
	if (!image)
		return;

	VDStringW wpath = VDTextU8ToW(VDStringSpanA(utf8path));
	VDFileStream fs(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	ATSaveCassetteImageCAS(fs, image);

	cas.SetImagePersistent(wpath.c_str());
	cas.SetImageClean();
}

static void TapeEditorSaveWAV(const char *utf8path) {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	IATCassetteImage *image = cas.GetImage();
	if (!image)
		return;

	VDStringW wpath = VDTextU8ToW(VDStringSpanA(utf8path));
	VDFileStream fs(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	ATSaveCassetteImageWAV(fs, image);

	cas.SetImagePersistent(wpath.c_str());
	cas.SetImageClean();
}

static void TapeEditorExtractCFile(const char *utf8path) {
	vdfastvector<uint8> data;
	g_tapeEditor.ExtractSelectionAsCFile(data);

	if (data.empty())
		throw MyError("There is no data to extract.");

	VDStringW wpath = VDTextU8ToW(VDStringSpanA(utf8path));
	VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(data.data(), (long)data.size());
	f.close();
}

// ---- Process file dialog results ----

static void TapeEditorPollFileDialogs() {
	TapeFileAction action = TapeFileAction::None;
	std::string path;

	{
		std::lock_guard<std::mutex> lock(g_tapeEditorMutex);
		action = g_tapeFileAction;
		path = std::move(g_tapeFilePath);
		g_tapeFileAction = TapeFileAction::None;
		g_tapeFilePath.clear();
	}

	if (action == TapeFileAction::None)
		return;

	try {
		switch (action) {
			case TapeFileAction::Open:
				TapeEditorLoad(path.c_str());
				break;
			case TapeFileAction::SaveCAS:
				TapeEditorSaveCAS(path.c_str());
				break;
			case TapeFileAction::SaveWAV:
				TapeEditorSaveWAV(path.c_str());
				break;
			case TapeFileAction::ExtractCFile:
				TapeEditorExtractCFile(path.c_str());
				break;
			default:
				break;
		}
	} catch (const MyError& e) {
		LOG_ERROR("TapeEditor", "File operation failed: %s", e.c_str());
	}
}

// ---- Status bar text ----

static void TapeEditorStatusBar() {
	const auto& te = g_tapeEditor;
	const bool hasNonEmptySel = te.HasNonEmptySelection();

	const char *statusText = nullptr;
	char statusBuf[256];

	switch (te.mDrawMode) {
		case TapeDrawMode::Scroll:
			statusText = "Scroll tool: left-drag to scroll to other parts of the tape";
			break;
		case TapeDrawMode::Draw:
			statusText = "Draw tool: left-click or drag to set or reset bits";
			break;
		case TapeDrawMode::Select:
			if (!hasNonEmptySel) {
				statusText = "Select tool: left-drag to select a region of bits";
				break;
			}
			goto show_selection;
		case TapeDrawMode::Insert:
			if (!hasNonEmptySel) {
				statusText = "Insert tool: left-drag to the right to insert tape";
				break;
			}
			goto show_selection;
		case TapeDrawMode::Analyze:
			if (!hasNonEmptySel) {
				statusText = "Analyze tool: left-drag over range to decode as standard bytes";
				break;
			}
			goto show_selection;
	}

	if (statusText) {
		ImGui::TextUnformatted(statusText);
		return;
	}

show_selection:
	{
		const uint32 selStart = te.mSelSortedStartSample;
		const uint32 selEnd = te.mSelSortedEndSample;
		const float selStartTime = (float)selStart * kATCassetteSecondsPerDataSample;
		const float selEndTime = (float)selEnd * kATCassetteSecondsPerDataSample;

		snprintf(statusBuf, sizeof(statusBuf),
			"Selected %.3fs in range %.3fs-%.3fs | %u sample%s in %u-%u",
			selEndTime - selStartTime,
			selStartTime,
			selEndTime,
			selEnd - selStart,
			(selEnd - selStart != 1) ? "s" : "",
			selStart,
			selEnd
		);

		ImGui::TextUnformatted(statusBuf);
	}
}

// ---- Title bar text ----

static void TapeEditorGetTitle(char *buf, size_t bufSize) {
	ATCassetteEmulator& cas = g_sim.GetCassette();

	if (!cas.GetImage()) {
		snprintf(buf, bufSize, "Tape Editor");
		return;
	}

	const wchar_t *path = cas.GetPath();
	bool dirty = cas.IsImageDirty();

	VDStringA name;
	if (path && *path) {
		const wchar_t *fname = VDFileSplitPath(path);
		name = VDTextWToU8(VDStringSpanW(fname));
	} else {
		name = "(new tape)";
	}

	snprintf(buf, bufSize, "Tape Editor - %s%s", dirty ? "*" : "", name.c_str());
}

// ---- Main render function ----

void ATUIRenderTapeEditor(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	TapeEditorInit();
	TapeEditorPollFileDialogs();
	TapeEditorSyncImage();

	auto& te = g_tapeEditor;
	ATCassetteEmulator& cas = sim.GetCassette();
	const bool hasImage = te.mpImage != nullptr;
	const bool hasNonEmptySel = hasImage && te.HasNonEmptySelection();

	// Window title
	char title[256];
	TapeEditorGetTitle(title, sizeof(title));

	ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool windowOpen = state.showTapeEditor;
	if (!ImGui::Begin(title, &windowOpen, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) {
		if (!windowOpen) {
			TapeEditorSaveSettings();
			state.showTapeEditor = false;
		}
		ImGui::End();
		return;
	}

	if (!windowOpen || ATUICheckEscClose()) {
		TapeEditorSaveSettings();
		state.showTapeEditor = false;
		ImGui::End();
		return;
	}

	// ---- Menu bar ----
	if (ImGui::BeginMenuBar()) {
		// File menu
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New")) {
				if (TapeEditorOKToDiscard())
					TapeEditorNew();
				else {
					g_tapePendingAction = TapePendingAction::New;
					g_tapeDiscardPending = true;
				}
			}

			if (ImGui::MenuItem("Open...")) {
				if (TapeEditorOKToDiscard()) {
					static const SDL_DialogFileFilter kFilters[] = {
						{ "Cassette Images", "cas;wav;flac;ogg" },
						{ "All Files", "*" },
					};
					SDL_ShowOpenFileDialog(TapeEditorFileCallback, (void*)(uintptr_t)TapeFileAction::Open, window, kFilters, 2, nullptr, false);
				} else {
					g_tapePendingAction = TapePendingAction::Open;
					g_tapeDiscardPending = true;
				}
			}

			if (ImGui::MenuItem("Reload", nullptr, false, hasImage && cas.IsImagePersistent())) {
				if (TapeEditorOKToDiscard()) {
					try { TapeEditorReload(); }
					catch (const MyError& e) { LOG_ERROR("TapeEditor", "Reload failed: %s", e.c_str()); }
				} else {
					g_tapePendingAction = TapePendingAction::Reload;
					g_tapeDiscardPending = true;
				}
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Save As CAS...", nullptr, false, hasImage)) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Atari Cassette Image", "cas" },
				};
				SDL_ShowSaveFileDialog(TapeEditorFileCallback, (void*)(uintptr_t)TapeFileAction::SaveCAS, window, kFilters, 1, nullptr);
			}

			if (ImGui::MenuItem("Save As WAV...", nullptr, false, hasImage)) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "Waveform Audio", "wav" },
				};
				SDL_ShowSaveFileDialog(TapeEditorFileCallback, (void*)(uintptr_t)TapeFileAction::SaveWAV, window, kFilters, 1, nullptr);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Close")) {
				TapeEditorSaveSettings();
				state.showTapeEditor = false;
			}

			ImGui::EndMenu();
		}

		// Edit menu
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, hasImage && te.CanUndo()))
				te.Undo();

			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, hasImage && te.CanRedo()))
				te.Redo();

			ImGui::Separator();

			if (ImGui::MenuItem("Select All", "Ctrl+A", false, hasImage))
				te.SelectAll();

			if (ImGui::MenuItem("Deselect", nullptr, false, hasImage))
				te.DeselectAll();

			ImGui::Separator();

			if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasNonEmptySel))
				te.Cut();

			if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasNonEmptySel))
				te.Copy();

			if (ImGui::MenuItem("Copy Decoded Data", nullptr, false, te.HasDecodedData()))
				te.CopyDecodedData();

			if (ImGui::MenuItem("Paste", "Ctrl+V", false, hasImage && te.HasClip()))
				te.Paste();

			if (ImGui::MenuItem("Delete", "Del", false, hasNonEmptySel))
				te.Delete();

			ImGui::Separator();

			if (ImGui::MenuItem("Convert to Standard Block", nullptr, false, !te.mbShowTurboData && hasNonEmptySel))
				te.ConvertToStdBlock();

			if (ImGui::MenuItem("Convert to Raw Block", nullptr, false, !te.mbShowTurboData && hasNonEmptySel))
				te.ConvertToRawBlock();

			ImGui::Separator();

			if (ImGui::MenuItem("Repeat Last Analysis", nullptr, false, hasImage))
				te.ReAnalyze();

			if (ImGui::MenuItem("Repeat Last Analysis (Flip)", nullptr, false, hasImage))
				te.ReAnalyzeFlip();

			ImGui::EndMenu();
		}

		// View menu
		if (ImGui::BeginMenu("View")) {
			bool fskData = !te.mbShowTurboData;
			bool turboData = te.mbShowTurboData;

			if (ImGui::MenuItem("FSK Data", nullptr, fskData))
				te.mbShowTurboData = false;

			if (ImGui::MenuItem("Turbo Data", nullptr, turboData))
				te.mbShowTurboData = true;

			ImGui::Separator();

			bool noSignal = te.mWaveformMode == TapeWaveformMode::None;
			bool waveform = te.mWaveformMode == TapeWaveformMode::Waveform;
			bool spectrogram = te.mWaveformMode == TapeWaveformMode::Spectrogram;

			if (ImGui::MenuItem("No Signal", nullptr, noSignal))
				te.mWaveformMode = TapeWaveformMode::None;

			if (ImGui::MenuItem("Waveform", nullptr, waveform))
				te.mWaveformMode = TapeWaveformMode::Waveform;

			if (ImGui::MenuItem("Spectrogram", nullptr, spectrogram))
				te.mWaveformMode = TapeWaveformMode::Spectrogram;

			ImGui::Separator();

			bool freqGuide = te.mbShowFrequencyGuidelines;
			if (ImGui::MenuItem("Show Frequency Guidelines", nullptr, &freqGuide, spectrogram))
				te.mbShowFrequencyGuidelines = freqGuide;

			ImGui::EndMenu();
		}

		// Data menu
		if (ImGui::BeginMenu("Data")) {
			if (ImGui::MenuItem("Extract C: File...", nullptr, false, hasNonEmptySel)) {
				static const SDL_DialogFileFilter kFilters[] = {
					{ "All Files", "*" },
				};
				SDL_ShowSaveFileDialog(TapeEditorFileCallback, (void*)(uintptr_t)TapeFileAction::ExtractCFile, window, kFilters, 1, nullptr);
			}

			ImGui::EndMenu();
		}

		// Options menu
		if (ImGui::BeginMenu("Options")) {
			bool sioCapture = te.mbSIOMonitorEnabled;
			if (ImGui::MenuItem("Capture SIO Tape Decoding", nullptr, &sioCapture))
				te.SetSIOMonitorEnabled(sioCapture);

			bool storeWaveform = te.mbStoreWaveformOnLoad;
			if (ImGui::MenuItem("Store Waveform On Load", nullptr, &storeWaveform))
				te.mbStoreWaveformOnLoad = storeWaveform;

			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	// ---- Toolbar ----
	{
		auto ToolButton = [](const char *label, bool active) -> bool {
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			}
			bool clicked = ImGui::Button(label);
			if (active)
				ImGui::PopStyleColor();
			return clicked;
		};

		if (ToolButton("Scroll", te.mDrawMode == TapeDrawMode::Scroll))
			te.mDrawMode = TapeDrawMode::Scroll;
		ImGui::SameLine();

		if (ToolButton("Draw", te.mDrawMode == TapeDrawMode::Draw))
			te.mDrawMode = TapeDrawMode::Draw;
		ImGui::SameLine();

		if (ToolButton("Select", te.mDrawMode == TapeDrawMode::Select))
			te.mDrawMode = TapeDrawMode::Select;
		ImGui::SameLine();

		if (ToolButton("Insert", te.mDrawMode == TapeDrawMode::Insert))
			te.mDrawMode = TapeDrawMode::Insert;
		ImGui::SameLine();

		// Analyze dropdown
		{
			const char *analyzeLabel = "Analyze";
			switch (te.mAnalysisDecoder) {
				case TapeDecoder::FSK_Sync: analyzeLabel = "Analyze (FSK sync)"; break;
				case TapeDecoder::FSK_PLL:  analyzeLabel = "Analyze (FSK PLL)"; break;
				case TapeDecoder::T2000:    analyzeLabel = "Analyze (T2000)"; break;
			}

			bool isAnalyze = te.mDrawMode == TapeDrawMode::Analyze;
			if (isAnalyze)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

			if (ImGui::Button(analyzeLabel))
				te.mDrawMode = TapeDrawMode::Analyze;

			if (isAnalyze)
				ImGui::PopStyleColor();

			ImGui::SameLine();
			if (ImGui::ArrowButton("##analyze_drop", ImGuiDir_Down))
				ImGui::OpenPopup("AnalyzePopup");

			if (ImGui::BeginPopup("AnalyzePopup")) {
				if (ImGui::MenuItem("FSK (using sync)")) {
					te.mAnalysisDecoder = TapeDecoder::FSK_Sync;
					te.mDrawMode = TapeDrawMode::Analyze;
				}
				if (ImGui::MenuItem("FSK (using PLL)")) {
					te.mAnalysisDecoder = TapeDecoder::FSK_PLL;
					te.mDrawMode = TapeDrawMode::Analyze;
				}
				if (ImGui::MenuItem("T2000")) {
					te.mAnalysisDecoder = TapeDecoder::T2000;
					te.mDrawMode = TapeDrawMode::Analyze;
				}
				ImGui::EndPopup();
			}
		}

		ImGui::SameLine();
		ImGui::Text("|");
		ImGui::SameLine();

		if (ImGui::Button("Delete") && hasNonEmptySel)
			te.Delete();
		ImGui::SameLine();

		// Filter dropdown
		if (ImGui::Button("Filter"))
			ImGui::OpenPopup("FilterPopup");

		if (ImGui::BeginPopup("FilterPopup")) {
			if (ImGui::MenuItem("FSK direct sample (~2000 baud)", nullptr, false, hasNonEmptySel)) {
				try { te.Filter(TapeFilterMode::FSKDirectSample2000Baud); }
				catch (const MyError& e) { LOG_ERROR("TapeEditor", "Filter failed: %s", e.c_str()); }
			}
			if (ImGui::MenuItem("FSK direct sample (~1000 baud)", nullptr, false, hasNonEmptySel)) {
				try { te.Filter(TapeFilterMode::FSKDirectSample1000Baud); }
				catch (const MyError& e) { LOG_ERROR("TapeEditor", "Filter failed: %s", e.c_str()); }
			}
			ImGui::EndPopup();
		}
	}

	// ---- Tape view ----
	{
		ImVec2 avail = ImGui::GetContentRegionAvail();
		float statusHeight = ImGui::GetTextLineHeightWithSpacing() + 4;
		float viewHeight = avail.y - statusHeight;

		if (viewHeight > 10) {
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			te.RenderTapeView(avail.x, viewHeight, cursor.x, cursor.y);
		}
	}

	// ---- Status bar ----
	TapeEditorStatusBar();

	// ---- Keyboard shortcuts (when tape editor is focused, no popup active) ----
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
		ImGuiIO& io = ImGui::GetIO();

		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift)
			te.Undo();
		if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift)))
			te.Redo();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
			te.SelectAll();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X))
			te.Cut();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
			te.Copy();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
			te.Paste();
		if (ImGui::IsKeyPressed(ImGuiKey_Delete))
			te.Delete();
	}

	// ---- Discard confirmation popup ----
	if (g_tapeDiscardPending) {
		ImGui::OpenPopup("Unsaved Tape##TapeDiscard");
		g_tapeDiscardPending = false;
	}

	if (ImGui::BeginPopupModal("Unsaved Tape##TapeDiscard", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("The current tape has not been saved. Continue anyway?");
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();

			try {
				switch (g_tapePendingAction) {
					case TapePendingAction::New:
						TapeEditorNew();
						break;
					case TapePendingAction::Open: {
						static const SDL_DialogFileFilter kFilters[] = {
							{ "Cassette Images", "cas;wav;flac;ogg" },
							{ "All Files", "*" },
						};
						SDL_ShowOpenFileDialog(TapeEditorFileCallback, (void*)(uintptr_t)TapeFileAction::Open, window, kFilters, 2, nullptr, false);
						break;
					}
					case TapePendingAction::Reload:
						TapeEditorReload();
						break;
					default:
						break;
				}
			} catch (const MyError& e) {
				LOG_ERROR("TapeEditor", "Operation failed: %s", e.c_str());
			}

			g_tapePendingAction = TapePendingAction::None;
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
			g_tapePendingAction = TapePendingAction::None;
		}

		ImGui::EndPopup();
	}

	ImGui::End();
}
