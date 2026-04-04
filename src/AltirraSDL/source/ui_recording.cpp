//	AltirraSDL - Recording functionality
//	Audio, SAP, and video recording state, start/stop, and settings dialogs.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/registry.h>
#include <vd2/system/math.h>
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>
#include <at/atcore/audiomixer.h>

#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"
#include "audiowriter.h"
#include "videowriter.h"
#include "sapwriter.h"
#include "vgmwriter.h"
#include "simeventmanager.h"

#include <algorithm>
#include <cmath>

extern ATSimulator g_sim;

// =========================================================================
// Audio recording state
// =========================================================================

static ATAudioWriter *g_pAudioWriter = nullptr;
static IATSAPWriter *g_pSAPWriter = nullptr;
static IATVideoWriter *g_pVideoWriter = nullptr;
static vdrefptr<IATVgmWriter> g_pVGMWriter;

// Video recording settings dialog state
static bool g_showVideoRecordingDialog = false;
static ATVideoEncoding g_videoRecEncoding = kATVideoEncoding_ZMBV;
static ATVideoRecordingFrameRate g_videoRecFrameRate = kATVideoRecordingFrameRate_Normal;
static ATVideoRecordingResamplingMode g_videoRecResamplingMode = ATVideoRecordingResamplingMode::Nearest;
static ATVideoRecordingAspectRatioMode g_videoRecAspectRatioMode = ATVideoRecordingAspectRatioMode::IntegerOnly;
static ATVideoRecordingScalingMode g_videoRecScalingMode = ATVideoRecordingScalingMode::None;
static bool g_videoRecHalfRate = false;
static bool g_videoRecEncodeAll = false;

bool ATUIIsRecording() {
	return g_pAudioWriter || g_pSAPWriter || g_pVideoWriter || g_pVGMWriter;
}

void ATUIStartAudioRecording(const wchar_t *path, bool raw) {
	if (ATUIIsRecording()) return;

	bool stereo = g_sim.IsDualPokeysEnabled();
	bool pal = (g_sim.GetVideoStandard() == kATVideoStandard_PAL);

	try {
		g_pAudioWriter = new ATAudioWriter(path, raw, stereo, pal, nullptr);
		g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		fprintf(stderr, "[AltirraSDL] Audio recording started (%s, %s)\n",
			raw ? "raw" : "WAV", stereo ? "stereo" : "mono");
	} catch (...) {
		delete g_pAudioWriter;
		g_pAudioWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start audio recording\n");
	}
}

void ATUIStartSAPRecording(const wchar_t *path) {
	if (ATUIIsRecording()) return;

	bool pal = (g_sim.GetVideoStandard() == kATVideoStandard_PAL);

	try {
		g_pSAPWriter = ATCreateSAPWriter();
		g_pSAPWriter->Init(g_sim.GetEventManager(), &g_sim.GetPokey(), nullptr, path, pal);
		fprintf(stderr, "[AltirraSDL] SAP recording started\n");
	} catch (...) {
		delete g_pSAPWriter;
		g_pSAPWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start SAP recording\n");
	}
}

void ATUIStartVGMRecording(const wchar_t *path) {
	if (ATUIIsRecording()) return;

	try {
		g_pVGMWriter = ATCreateVgmWriter();
		g_pVGMWriter->Init(path, g_sim);
		fprintf(stderr, "[AltirraSDL] VGM recording started\n");
	} catch (...) {
		g_pVGMWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start VGM recording\n");
	}
}

void ATUIStartVideoRecording(const wchar_t *path, ATVideoEncoding encoding) {
	if (ATUIIsRecording()) return;

	try {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();

		ATCreateVideoWriter(&g_pVideoWriter);

		int w;
		int h;
		bool rgb32;
		gtia.GetRawFrameFormat(w, h, rgb32);

		uint32 palette[256];
		if (!rgb32)
			gtia.GetPalette(palette);

		const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;
		VDFraction frameRate = hz50 ? VDFraction(1773447, 114*312) : VDFraction(3579545, 2*114*262);
		double samplingRate = hz50 ? 1773447.0 / 28.0 : 3579545.0 / 56.0;

		switch(g_videoRecFrameRate) {
			case kATVideoRecordingFrameRate_NTSCRatio:
				if (hz50) {
					samplingRate = samplingRate * (50000.0 / 1001.0) / frameRate.asDouble();
					frameRate = VDFraction(50000, 1001);
				} else {
					samplingRate = samplingRate * (60000.0 / 1001.0) / frameRate.asDouble();
					frameRate = VDFraction(60000, 1001);
				}
				break;

			case kATVideoRecordingFrameRate_Integral:
				if (hz50) {
					samplingRate = samplingRate * 50.0 / frameRate.asDouble();
					frameRate = VDFraction(50, 1);
				} else {
					samplingRate = samplingRate * 60.0 / frameRate.asDouble();
					frameRate = VDFraction(60, 1);
				}
				break;

			default:
				break;
		}

		double par = 1.0;
		if (g_videoRecAspectRatioMode != ATVideoRecordingAspectRatioMode::None) {
			if (g_videoRecAspectRatioMode == ATVideoRecordingAspectRatioMode::FullCorrection) {
				par = gtia.GetPixelAspectRatio();
			} else {
				int px = 2, py = 2;
				gtia.GetPixelAspectMultiple(px, py);
				par = (double)py / (double)px;
			}
		}

		g_pVideoWriter->Init(path,
			encoding,
			0,	// videoBitRate (not used for AVI encodings)
			0,	// audioBitRate (not used for AVI encodings)
			w, h,
			frameRate,
			par,
			g_videoRecResamplingMode,
			g_videoRecScalingMode,
			rgb32 ? NULL : palette,
			samplingRate,
			g_sim.IsDualPokeysEnabled(),
			hz50 ? 1773447.0f : 1789772.5f,
			g_videoRecHalfRate,
			g_videoRecEncodeAll,
			nullptr);

		g_sim.GetAudioOutput()->SetAudioTap(g_pVideoWriter->AsAudioTap());
		gtia.AddVideoTap(g_pVideoWriter->AsVideoTap());

		fprintf(stderr, "[AltirraSDL] Video recording started\n");
	} catch (const MyError& e) {
		if (g_pVideoWriter) {
			ATGTIAEmulator& gtia2 = g_sim.GetGTIA();
			gtia2.RemoveVideoTap(g_pVideoWriter->AsVideoTap());
			g_sim.GetAudioOutput()->SetAudioTap(nullptr);
			try { g_pVideoWriter->Shutdown(); } catch (...) {}
			delete g_pVideoWriter;
			g_pVideoWriter = nullptr;
		}
		fprintf(stderr, "[AltirraSDL] Failed to start video recording: %s\n", e.c_str());
	} catch (...) {
		if (g_pVideoWriter) {
			ATGTIAEmulator& gtia2 = g_sim.GetGTIA();
			gtia2.RemoveVideoTap(g_pVideoWriter->AsVideoTap());
			g_sim.GetAudioOutput()->SetAudioTap(nullptr);
			try { g_pVideoWriter->Shutdown(); } catch (...) {}
			delete g_pVideoWriter;
			g_pVideoWriter = nullptr;
		}
		fprintf(stderr, "[AltirraSDL] Failed to start video recording\n");
	}
}

void ATUIStopRecording() {
	bool wasRecording = ATUIIsRecording();

	if (g_pVideoWriter) {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.RemoveVideoTap(g_pVideoWriter->AsVideoTap());
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);

		try {
			g_pVideoWriter->Shutdown();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing video recording\n");
		}
		delete g_pVideoWriter;
		g_pVideoWriter = nullptr;
	}

	if (g_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		try {
			g_pAudioWriter->Finalize();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing audio recording\n");
		}
		delete g_pAudioWriter;
		g_pAudioWriter = nullptr;
	}

	if (g_pSAPWriter) {
		try {
			g_pSAPWriter->Shutdown();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing SAP recording\n");
		}
		delete g_pSAPWriter;
		g_pSAPWriter = nullptr;
	}

	if (g_pVGMWriter) {
		try {
			g_pVGMWriter->Shutdown();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing VGM recording\n");
		}
		g_pVGMWriter = nullptr;
	}

	if (wasRecording)
		fprintf(stderr, "[AltirraSDL] Recording stopped\n");
}

// =========================================================================
// Recording accessors — used by ui_menus.cpp
// =========================================================================

bool ATUIIsRecordingPaused() {
	return g_pVideoWriter && g_pVideoWriter->IsPaused();
}

void ATUIToggleRecordingPause() {
	if (g_pVideoWriter) {
		if (g_pVideoWriter->IsPaused())
			g_pVideoWriter->Resume();
		else
			g_pVideoWriter->Pause();
	}
}

bool ATUIIsVideoRecording() {
	return g_pVideoWriter != nullptr;
}

void ATUIShowVideoRecordingDialog() {
	g_showVideoRecordingDialog = true;
}

// =========================================================================
// Video Recording Settings dialog (ImGui replacement for IDD_VIDEO_RECORDING)
// =========================================================================

void ATUIRenderVideoRecordingDialog(SDL_Window *window) {
	if (!g_showVideoRecordingDialog)
		return;

	ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Record Video", &g_showVideoRecordingDialog, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_showVideoRecordingDialog = false;
		ImGui::End();
		return;
	}

	// Load saved settings from registry
	static bool settingsLoaded = false;
	if (!settingsLoaded) {
		VDRegistryAppKey key("Settings");
		g_videoRecEncoding = (ATVideoEncoding)key.getEnumInt("Video Recording: Compression Mode", kATVideoEncodingCount, kATVideoEncoding_ZMBV);
		// Clamp to AVI-only encodings for SDL3 build
		if (g_videoRecEncoding > kATVideoEncoding_ZMBV)
			g_videoRecEncoding = kATVideoEncoding_ZMBV;
		g_videoRecFrameRate = (ATVideoRecordingFrameRate)key.getEnumInt("Video Recording: Frame Rate", kATVideoRecordingFrameRateCount, kATVideoRecordingFrameRate_Normal);
		g_videoRecHalfRate = key.getBool("Video Recording: Half Rate", false);
		g_videoRecEncodeAll = key.getBool("Video Recording: Encode All Frames", false);
		g_videoRecAspectRatioMode = (ATVideoRecordingAspectRatioMode)key.getEnumInt("Video Recording: Aspect Ratio Mode", (int)ATVideoRecordingAspectRatioMode::Count, (int)ATVideoRecordingAspectRatioMode::IntegerOnly);
		g_videoRecResamplingMode = (ATVideoRecordingResamplingMode)key.getEnumInt("Video Recording: Resampling Mode", (int)ATVideoRecordingResamplingMode::Count, (int)ATVideoRecordingResamplingMode::Nearest);
		g_videoRecScalingMode = (ATVideoRecordingScalingMode)key.getEnumInt("Video Recording: Frame Size Mode", (int)ATVideoRecordingScalingMode::Count, (int)ATVideoRecordingScalingMode::None);
		settingsLoaded = true;
	}

	// Video codec
	static const char *kCodecNames[] = {
		"Uncompressed (AVI)",
		"Run-Length Encoding (AVI)",
		"Zipped Motion Block Vector (AVI)",
	};
	int codecIdx = (int)g_videoRecEncoding;
	if (codecIdx > 2) codecIdx = 2;
	if (ImGui::Combo("Video Codec", &codecIdx, kCodecNames, 3))
		g_videoRecEncoding = (ATVideoEncoding)codecIdx;

	// Codec description
	static const char *kCodecDescs[] = {
		"Uncompressed RGB. Largest files, most compatible.",
		"Lossless RLE compression. Smaller than raw, 8-bit video only.",
		"Lossless ZMBV (DOSBox). Excellent compression for retro video.\nNeeds ffmpeg/ffdshow to play.",
	};
	ImGui::TextWrapped("%s", kCodecDescs[codecIdx]);
	ImGui::Separator();

	// Frame rate
	const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;
	double halfMul = g_videoRecHalfRate ? 0.5 : 1.0;

	static const double kFrameRates[][2]={
		{ 3579545.0 / (2.0*114.0*262.0), 1773447.0 / (114.0*312.0) },
		{ 60000.0/1001.0, 50000.0/1001.0 },
		{ 60.0, 50.0 },
	};

	char frlabel0[64], frlabel1[64], frlabel2[64];
	snprintf(frlabel0, sizeof frlabel0, "Accurate (%.3f fps)", kFrameRates[0][hz50] * halfMul);
	snprintf(frlabel1, sizeof frlabel1, "Broadcast (%.3f fps)", kFrameRates[1][hz50] * halfMul);
	snprintf(frlabel2, sizeof frlabel2, "Integral (%.3f fps)", kFrameRates[2][hz50] * halfMul);

	int frIdx = (int)g_videoRecFrameRate;
	ImGui::Text("Frame Rate:");
	ImGui::RadioButton(frlabel0, &frIdx, 0);
	ImGui::RadioButton(frlabel1, &frIdx, 1);
	ImGui::RadioButton(frlabel2, &frIdx, 2);
	g_videoRecFrameRate = (ATVideoRecordingFrameRate)frIdx;

	ImGui::Checkbox("Record at half frame rate", &g_videoRecHalfRate);
	ImGui::Checkbox("Encode duplicate frames as full frames", &g_videoRecEncodeAll);
	ImGui::Separator();

	// Scaling
	static const char *kScalingModes[] = {
		"No scaling",
		"Scale to 640x480 (4:3)",
		"Scale to 854x480 (16:9)",
		"Scale to 960x720 (4:3)",
		"Scale to 1280x720 (16:9)",
	};
	int scalingIdx = (int)g_videoRecScalingMode;
	ImGui::Combo("Scaling", &scalingIdx, kScalingModes, 5);
	g_videoRecScalingMode = (ATVideoRecordingScalingMode)scalingIdx;

	// Aspect ratio
	static const char *kAspectModes[] = {
		"None - raw pixels",
		"Pixel double - 1x/2x only",
		"Full - correct pixel aspect ratio",
	};
	int arIdx = (int)g_videoRecAspectRatioMode;
	ImGui::Combo("Aspect Ratio", &arIdx, kAspectModes, 3);
	g_videoRecAspectRatioMode = (ATVideoRecordingAspectRatioMode)arIdx;

	// Resampling
	static const char *kResamplingModes[] = {
		"Nearest - sharpest",
		"Sharp Bilinear",
		"Bilinear - smoothest",
	};
	int rsIdx = (int)g_videoRecResamplingMode;
	ImGui::Combo("Resampling", &rsIdx, kResamplingModes, 3);
	g_videoRecResamplingMode = (ATVideoRecordingResamplingMode)rsIdx;

	ImGui::Separator();

	// OK/Cancel
	if (ImGui::Button("Record", ImVec2(120, 0))) {
		// Save settings
		VDRegistryAppKey key("Settings");
		key.setInt("Video Recording: Compression Mode", (int)g_videoRecEncoding);
		key.setInt("Video Recording: Frame Rate", (int)g_videoRecFrameRate);
		key.setBool("Video Recording: Half Rate", g_videoRecHalfRate);
		key.setBool("Video Recording: Encode All Frames", g_videoRecEncodeAll);
		key.setInt("Video Recording: Aspect Ratio Mode", (int)g_videoRecAspectRatioMode);
		key.setInt("Video Recording: Resampling Mode", (int)g_videoRecResamplingMode);
		key.setInt("Video Recording: Frame Size Mode", (int)g_videoRecScalingMode);

		g_showVideoRecordingDialog = false;

		// Show save file dialog
		static const SDL_DialogFileFilter aviFilters[] = {
			{ "AVI Video", "avi" }, { "All Files", "*" },
		};
		// Capture encoding to pass through the callback
		static ATVideoEncoding s_pendingEncoding;
		s_pendingEncoding = g_videoRecEncoding;
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordVideo, fl[0], (int)s_pendingEncoding);
		}, nullptr, window, aviFilters, 1, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_showVideoRecordingDialog = false;

	ImGui::End();
}

// =========================================================================
// Audio Options dialog
// Matches Windows IDD_AUDIO_OPTIONS (uiaudiooptions.cpp)
// =========================================================================

void ATUIRenderAudioOptionsDialog(ATUIState &state) {
	if (!state.showAudioOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 320), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Audio Options", &state.showAudioOptions, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAudioOptions = false;
		ImGui::End();
		return;
	}

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	if (!audioOut) {
		ImGui::Text("No audio output available.");
		ImGui::End();
		return;
	}

	// Volume slider (0 to 200 ticks = -20dB to 0dB)
	float volume = audioOut->GetVolume();
	int volTick = 200 + VDRoundToInt(100.0f * log10f(std::max(volume, 0.01f)));
	volTick = std::clamp(volTick, 0, 200);
	if (ImGui::SliderInt("Volume", &volTick, 0, 200)) {
		audioOut->SetVolume(powf(10.0f, (volTick - 200) * 0.01f));
	}
	ImGui::SameLine();
	ImGui::Text("%.1fdB", 0.1f * (volTick - 200));

	// Drive volume slider
	float driveVol = audioOut->GetMixLevel(kATAudioMix_Drive);
	int driveVolTick = 200 + VDRoundToInt(100.0f * log10f(std::max(driveVol, 0.01f)));
	driveVolTick = std::clamp(driveVolTick, 0, 200);
	if (ImGui::SliderInt("Drive volume", &driveVolTick, 0, 200)) {
		audioOut->SetMixLevel(kATAudioMix_Drive, powf(10.0f, (driveVolTick - 200) * 0.01f));
	}
	ImGui::SameLine();
	ImGui::Text("%.1fdB", 0.1f * (driveVolTick - 200));

	// Covox volume slider
	float covoxVol = audioOut->GetMixLevel(kATAudioMix_Covox);
	int covoxVolTick = 200 + VDRoundToInt(100.0f * log10f(std::max(covoxVol, 0.01f)));
	covoxVolTick = std::clamp(covoxVolTick, 0, 200);
	if (ImGui::SliderInt("Covox volume", &covoxVolTick, 0, 200)) {
		audioOut->SetMixLevel(kATAudioMix_Covox, powf(10.0f, (covoxVolTick - 200) * 0.01f));
	}
	ImGui::SameLine();
	ImGui::Text("%.1fdB", 0.1f * (covoxVolTick - 200));

	ImGui::Separator();

	// Latency slider (10ms to 500ms)
	int latency = audioOut->GetLatency();
	int latencyTick = (latency + 5) / 10;
	latencyTick = std::clamp(latencyTick, 1, 50);
	if (ImGui::SliderInt("Latency", &latencyTick, 1, 50)) {
		audioOut->SetLatency(latencyTick * 10);
	}
	ImGui::SameLine();
	ImGui::Text("%d ms", latencyTick * 10);

	// Extra buffer slider (20ms to 500ms)
	int extraBuf = audioOut->GetExtraBuffer();
	int extraBufTick = (extraBuf + 5) / 10;
	extraBufTick = std::clamp(extraBufTick, 2, 50);
	if (ImGui::SliderInt("Extra buffer", &extraBufTick, 2, 50)) {
		audioOut->SetExtraBuffer(extraBufTick * 10);
	}
	ImGui::SameLine();
	ImGui::Text("%d ms", extraBufTick * 10);

	ImGui::Separator();

	bool debug = g_sim.IsAudioStatusEnabled();
	if (ImGui::Checkbox("Show debug info", &debug))
		g_sim.SetAudioStatusEnabled(debug);

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showAudioOptions = false;

	ImGui::End();
}
