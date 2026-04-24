//	AltirraSDL - Palette Solver dialog
//	Matches Windows ATUIColorImageReferenceWindow from uicolors.cpp.
//	Loads a reference image (PNG or .pal/.act file), samples a 16x16 color
//	grid, and runs ATColorPaletteSolver to derive generation parameters.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "ui_file_dialog_sdl3.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <cmath>

#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>
#include <vd2/system/text.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>

#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"
#include "uiaccessors.h"
#include "palettesolver.h"
#include "palettegenerator.h"
#include "common_png.h"
#include "decode_png.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// =========================================================================
// Palette Solver state
// =========================================================================

namespace {

// Reference image data
struct PaletteSolverState {
	// Reference palette (sampled from image or loaded from .pal file)
	bool hasReference = false;
	uint32 referencePalette[256] = {};
	VDStringA referenceFilename;

	// Solver controls
	bool lockHueStart = false;
	bool lockGamma = false;
	bool normalizeContrast = false;

	// Gain control (same as Windows: slider -100..+100, maps to pow(2, raw/100))
	float gain = 1.0f;
	int gainRaw = 0;

	// Solver thread state
	std::mutex solverMutex;
	std::thread solverThread;
	std::atomic<bool> solverRunning{false};
	std::atomic<bool> solverStopRequested{false};

	// Snapshot for solver thread (set on main thread before launch)
	ATColorParams solverInitialParams {};
	uint32 solverPalette[256] = {};  // gain-adjusted reference palette

	// Results (protected by solverMutex)
	bool hasImprovedSolution = false;
	ATColorParams improvedSolution {};
	uint32 currentError = ~0u;
	bool solverFinished = false;

	// Status message
	VDStringA statusText;

	// Pending reference load from file dialog callback (thread-safe handoff)
	std::mutex pendingLoadMutex;
	bool pendingLoadReady = false;
	bool pendingLoadSuccess = false;
	uint32 pendingPalette[256] = {};
	VDStringA pendingFilename;
	VDStringA pendingStatusText;

	void StopSolver() {
		solverStopRequested = true;
#if !defined(__EMSCRIPTEN__)
		if (solverThread.joinable())
			solverThread.join();
#endif
		solverRunning = false;
		solverStopRequested = false;
	}

	void Shutdown() {
		StopSolver();
		hasReference = false;
	}
};

static PaletteSolverState s_solver;

// =========================================================================
// Reference image loading
// =========================================================================

// Load a .pal or .act file (768 bytes = 256 colors x 3 RGB)
static bool LoadPaletteFile(const char *utf8path, uint32 outPal[256]) {
	try {
		VDStringW wpath = VDTextU8ToW(VDStringA(utf8path));
		VDFile f(wpath.c_str());
		sint64 size = f.size();
		if (size != 768)
			return false;

		uint8 buf[768];
		f.read(buf, 768);
		f.close();

		for (int i = 0; i < 256; ++i) {
			outPal[i] = ((uint32)buf[i * 3 + 0] << 16)
				| ((uint32)buf[i * 3 + 1] << 8)
				| ((uint32)buf[i * 3 + 2]);
		}
		return true;
	} catch (...) {
		return false;
	}
}

// Load a PNG file and sample a 16x16 grid to get 256 colors.
// Uses a simple grid sampling approach — the image is divided into a 16x16
// grid and each cell is averaged.  Gain is NOT applied here — it is applied
// at match time (same as Windows).
static bool LoadPNGAndSample(const char *utf8path, uint32 outPal[256]) {
	try {
		VDStringW wpath = VDTextU8ToW(VDStringA(utf8path));
		VDFile f(wpath.c_str());
		sint64 size = f.size();
		if (size <= 8 || size > 256 * 1024 * 1024)
			return false;

		vdblock<uint8> buf((uint32)size);
		f.read(buf.data(), (long)size);
		f.close();

		// Check PNG signature
		if (memcmp(buf.data(), nsVDPNG::kPNGSignature, 8) != 0)
			return false;

		vdautoptr decoder(VDCreateImageDecoderPNG());
		if (kPNGDecodeOK != decoder->Decode(buf.data(), (uint32)buf.size()))
			return false;

		const VDPixmap& px = decoder->GetFrameBuffer();
		if (px.w < 16 || px.h < 16)
			return false;

		// Convert to XRGB8888 if needed
		VDPixmapBuffer img(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);
		VDPixmapBlt(img, px);

		// Sample 16x16 grid — average pixels in each cell
		for (int row = 0; row < 16; ++row) {
			int y0 = row * img.h / 16;
			int y1 = (row + 1) * img.h / 16;
			if (y1 <= y0) y1 = y0 + 1;

			for (int col = 0; col < 16; ++col) {
				int x0 = col * img.w / 16;
				int x1 = (col + 1) * img.w / 16;
				if (x1 <= x0) x1 = x0 + 1;

				uint64 rSum = 0, gSum = 0, bSum = 0;
				int count = 0;

				for (int y = y0; y < y1 && y < img.h; ++y) {
					const uint32 *scanline = (const uint32 *)((const uint8 *)img.data + img.pitch * y);
					for (int x = x0; x < x1 && x < img.w; ++x) {
						uint32 c = scanline[x];
						rSum += (c >> 16) & 0xFF;
						gSum += (c >> 8) & 0xFF;
						bSum += c & 0xFF;
						++count;
					}
				}

				if (count > 0) {
					uint32 r = (uint32)(rSum / count);
					uint32 g = (uint32)(gSum / count);
					uint32 b = (uint32)(bSum / count);
					outPal[row * 16 + col] = (r << 16) | (g << 8) | b;
				} else {
					outPal[row * 16 + col] = 0;
				}
			}
		}

		return true;
	} catch (...) {
		return false;
	}
}

// =========================================================================
// File dialog callback
// =========================================================================

// File dialog callback — may be called from a non-main thread.
// Stage results for main-thread consumption via pendingLoad* fields.
static void PaletteSolverLoadCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;

	const char *path = filelist[0];
	uint32 tmpPal[256] = {};
	bool success = false;
	VDStringA statusMsg;

	// Check extension
	size_t len = strlen(path);
	bool isPalFile = false;
	if (len >= 4) {
		const char *ext = path + len - 4;
		isPalFile = (SDL_strcasecmp(ext, ".pal") == 0 || SDL_strcasecmp(ext, ".act") == 0);
	}

	if (isPalFile) {
		if (LoadPaletteFile(path, tmpPal)) {
			success = true;
			statusMsg = "Loaded palette file.";
		} else {
			statusMsg = "Failed to load palette file.";
		}
	} else {
		if (LoadPNGAndSample(path, tmpPal)) {
			success = true;
			statusMsg = "Loaded reference image.";
		} else {
			statusMsg = "Failed to load reference image (PNG only).";
		}
	}

	std::lock_guard<std::mutex> lock(s_solver.pendingLoadMutex);
	s_solver.pendingLoadSuccess = success;
	if (success)
		memcpy(s_solver.pendingPalette, tmpPal, sizeof tmpPal);
	s_solver.pendingFilename = path;
	s_solver.pendingStatusText = statusMsg;
	s_solver.pendingLoadReady = true;
}

// =========================================================================
// Solver thread
// =========================================================================

static void SolverThreadFunc() {
	// All shared state was snapshotted into s_solver.solverInitialParams
	// and s_solver.solverPalette on the main thread before launch.
	ATColorParams initialParams = s_solver.solverInitialParams;

	vdautoptr<IATColorPaletteSolver> solver(ATCreateColorPaletteSolver());
	solver->Init(initialParams, s_solver.solverPalette,
		s_solver.lockHueStart, s_solver.lockGamma);

	// Start with no color matching (Windows does this)
	initialParams.mColorMatchingMode = ATColorMatchingMode::None;
	solver->Reinit(initialParams);

	int pass = 0;
	while (!s_solver.solverStopRequested) {
		auto status = solver->Iterate();

		if (status == IATColorPaletteSolver::Status::RunningImproved) {
			std::lock_guard<std::mutex> lock(s_solver.solverMutex);
			s_solver.hasImprovedSolution = true;
			solver->GetCurrentSolution(s_solver.improvedSolution);
			s_solver.currentError = solver->GetCurrentError().value_or(0);
		}

		if (status == IATColorPaletteSolver::Status::Finished) {
			++pass;
			if (pass == 1) {
				// Second pass with sRGB color matching (matches Windows behavior)
				ATColorParams newInitial;
				solver->GetCurrentSolution(newInitial);
				newInitial.mColorMatchingMode = ATColorMatchingMode::SRGB;
				solver->Reinit(newInitial);
			} else {
				break;
			}
		}
	}

	// Final solution
	{
		std::lock_guard<std::mutex> lock(s_solver.solverMutex);
		solver->GetCurrentSolution(s_solver.improvedSolution);
		s_solver.currentError = solver->GetCurrentError().value_or(0);
		s_solver.hasImprovedSolution = true;
		s_solver.solverFinished = true;
	}

	s_solver.solverRunning = false;
}

} // anonymous namespace

// =========================================================================
// Render function
// =========================================================================

void ATUIRenderPaletteSolver(ATSimulator &sim, bool &open) {
	ImGui::SetNextWindowSize(ImVec2(520, 440), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Palette Solver", &open, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		// Stop solver on close
		if (s_solver.solverRunning)
			s_solver.StopSolver();
		open = false;
		ImGui::End();
		return;
	}

	// Apply pending reference load from file dialog callback (thread-safe handoff)
	{
		std::lock_guard<std::mutex> lock(s_solver.pendingLoadMutex);
		if (s_solver.pendingLoadReady) {
			if (s_solver.pendingLoadSuccess) {
				memcpy(s_solver.referencePalette, s_solver.pendingPalette, sizeof s_solver.referencePalette);
				s_solver.hasReference = true;
			}
			s_solver.referenceFilename = s_solver.pendingFilename;
			s_solver.statusText = s_solver.pendingStatusText;
			s_solver.pendingLoadReady = false;
		}
	}

	// -- Load Reference Picture --
	if (ImGui::Button("Load Reference Picture...")) {
		static const SDL_DialogFileFilter kFilters[] = {
			{ "Images (*.png;*.pal;*.act)", "png;pal;act" },
		};
		ATUIShowOpenFileDialog('cref', PaletteSolverLoadCallback,
			nullptr, g_pWindow, kFilters, 1, false);
	}

	if (s_solver.hasReference) {
		ImGui::SameLine();
		ImGui::TextDisabled("(loaded)");
	}

	ImGui::Separator();

	// -- Reference palette preview (if loaded) --
	if (s_solver.hasReference) {
		ImGui::Text("Reference Palette:");
		float cellSize = floorf(std::min(ImGui::GetContentRegionAvail().x / 16.0f, 12.0f));
		if (cellSize < 3.0f) cellSize = 3.0f;
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList *dl = ImGui::GetWindowDrawList();

		for (int row = 0; row < 16; ++row) {
			for (int col = 0; col < 16; ++col) {
				uint32 c = s_solver.referencePalette[row * 16 + col];
				ImU32 color = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
				ImVec2 p0(pos.x + col * cellSize, pos.y + row * cellSize);
				ImVec2 p1(p0.x + cellSize, p0.y + cellSize);
				dl->AddRectFilled(p0, p1, color);
			}
		}
		ImGui::Dummy(ImVec2(cellSize * 16.0f, cellSize * 16.0f));
	}

	ImGui::Separator();

	// -- Lock checkboxes --
	ImGui::Checkbox("Lock Hue Start", &s_solver.lockHueStart);
	ImGui::SameLine();
	ImGui::Checkbox("Lock Gamma Correction", &s_solver.lockGamma);

	// -- Normalize Contrast --
	ImGui::Checkbox("Normalize Contrast", &s_solver.normalizeContrast);

	// -- Gain slider --
	{
		if (ImGui::SliderInt("Gain", &s_solver.gainRaw, -100, 100, "%d")) {
			s_solver.gain = powf(2.0f, (float)s_solver.gainRaw / 100.0f);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Gain")) {
			s_solver.gainRaw = 0;
			s_solver.gain = 1.0f;
		}
	}

	ImGui::Separator();

	// -- Match / Stop button --
	bool canMatch = s_solver.hasReference && !s_solver.solverRunning;
	if (s_solver.solverRunning) {
		if (ImGui::Button("Stop")) {
			s_solver.StopSolver();
		}
	} else {
		if (!canMatch)
			ImGui::BeginDisabled();
		if (ImGui::Button("Match")) {
#if !defined(__EMSCRIPTEN__)
			// Join any previously finished thread before re-using the handle
			if (s_solver.solverThread.joinable())
				s_solver.solverThread.join();
#endif

			// Snapshot initial params on main thread (Bug #4: never read g_sim on bg thread)
			ATGTIAEmulator& gtia = sim.GetGTIA();
			ATColorSettings cs = gtia.GetColorSettings();
			s_solver.solverInitialParams = cs.mbUsePALParams && gtia.IsPALMode()
				? cs.mPALParams : cs.mNTSCParams;

			// Apply gain to reference palette for the solver (Bug #5: gain at match time)
			for (int i = 0; i < 256; ++i) {
				uint32 c = s_solver.referencePalette[i];
				uint32 r = std::min<uint32>(255, (uint32)((float)((c >> 16) & 0xFF) * s_solver.gain));
				uint32 g = std::min<uint32>(255, (uint32)((float)((c >> 8) & 0xFF) * s_solver.gain));
				uint32 b = std::min<uint32>(255, (uint32)((float)(c & 0xFF) * s_solver.gain));
				s_solver.solverPalette[i] = (r << 16) | (g << 8) | b;
			}

			s_solver.solverFinished = false;
			s_solver.hasImprovedSolution = false;
			s_solver.currentError = ~0u;
			s_solver.statusText.clear();
			s_solver.solverRunning = true;
			s_solver.solverStopRequested = false;
#if defined(__EMSCRIPTEN__)
			// WASM: run the solver synchronously on the main thread.
			// Palette matching is a one-shot dev tool that takes tens
			// of seconds at worst; freezing the UI while it runs is
			// acceptable on the browser target, which cannot spawn
			// pthreads.  A future refinement would be to call
			// solver.Iterate() incrementally from the render function
			// — good enough for the initial port.
			SolverThreadFunc();
#else
			s_solver.solverThread = std::thread(SolverThreadFunc);
#endif
		}
		if (!canMatch)
			ImGui::EndDisabled();
	}

	// -- Check for improved solutions from solver thread --
	{
		std::lock_guard<std::mutex> lock(s_solver.solverMutex);
		if (s_solver.hasImprovedSolution) {
			s_solver.hasImprovedSolution = false;

			ATGTIAEmulator& gtia = sim.GetGTIA();
			ATColorSettings settings = gtia.GetColorSettings();
			ATNamedColorParams& activeParams = settings.mbUsePALParams && gtia.IsPALMode()
				? settings.mPALParams : settings.mNTSCParams;

			static_cast<ATColorParams&>(activeParams) = s_solver.improvedSolution;

			if (s_solver.normalizeContrast)
				activeParams.mContrast = 1.0f - activeParams.mBrightness;

			// Clear preset tag since solver has overridden the values
			// (matches Windows: ForcePresetToCustom in solver callback)
			activeParams.mPresetTag.clear();

			gtia.SetColorSettings(settings);

			// Update status with error metric (matches Windows quality labels)
			uint32 rawError = s_solver.currentError;
			float stdError = sqrtf((float)rawError / 719.0f);

			const char *quality = "very poor";
			if (stdError < 2.5f)
				quality = "excellent";
			else if (stdError < 5.0f)
				quality = "very good";
			else if (stdError < 10.0f)
				quality = "good";
			else if (stdError < 15.0f)
				quality = "poor";

			char buf[128];
			snprintf(buf, sizeof(buf), "Error = %.6g (%s)%s",
				stdError, quality,
				s_solver.solverFinished ? " - Finished" : " - Running...");
			s_solver.statusText = buf;
		}
	}

	// -- Status display --
	if (!s_solver.statusText.empty()) {
		ImGui::Separator();
		ImGui::TextWrapped("%s", s_solver.statusText.c_str());
	}

	// Show current computed palette vs reference for visual comparison
	if (s_solver.hasReference && s_solver.currentError != ~0u) {
		ImGui::Separator();
		ImGui::Text("Computed Palette:");

		ATGTIAEmulator& gtia = sim.GetGTIA();
		uint32 computedPal[256];
		gtia.GetPalette(computedPal);

		float cellSize = floorf(std::min(ImGui::GetContentRegionAvail().x / 16.0f, 12.0f));
		if (cellSize < 3.0f) cellSize = 3.0f;
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList *dl = ImGui::GetWindowDrawList();

		for (int row = 0; row < 16; ++row) {
			for (int col = 0; col < 16; ++col) {
				uint32 c = computedPal[row * 16 + col];
				ImU32 color = IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
				ImVec2 p0(pos.x + col * cellSize, pos.y + row * cellSize);
				ImVec2 p1(p0.x + cellSize, p0.y + cellSize);
				dl->AddRectFilled(p0, p1, color);
			}
		}
		ImGui::Dummy(ImVec2(cellSize * 16.0f, cellSize * 16.0f));
	}

	ImGui::End();
}

void ATUIShutdownPaletteSolver() {
	s_solver.Shutdown();
}
