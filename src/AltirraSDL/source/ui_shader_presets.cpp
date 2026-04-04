//	AltirraSDL - Shader Preset Browser & Parameter UI
//	Provides a View menu submenu for loading librashader presets (.slangp/.glslp)
//	and a parameter panel for real-time adjustment of shader uniforms.
//
//	When a librashader preset is active, the built-in screen effects (scanlines,
//	bloom, mask, distortion) are bypassed — the entire post-processing pipeline
//	is delegated to the external shader chain.

#include <stdafx.h>
#include <imgui.h>
#include <SDL3/SDL.h>

#include "ui_main.h"
#include "display_backend.h"
#include "display_librashader.h"
#include "logging.h"

#include <vd2/system/registry.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>

#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <cstring>
#include <cstdlib>

extern SDL_Window *g_pWindow;

// =========================================================================
// Helpers: VDStringA <-> std::string conversion
// =========================================================================

static std::string WToStd(const VDStringW &ws) {
	VDStringA u8 = VDTextWToU8(ws);
	return std::string(u8.c_str(), u8.size());
}

// =========================================================================
// Recent presets list — persisted in settings.ini under "Shader Presets"
// =========================================================================

static constexpr int kMaxRecentPresets = 5;
static std::vector<std::string> s_recentPresets;
static bool s_recentPresetsLoaded = false;

static void LoadRecentPresets() {
	if (s_recentPresetsLoaded)
		return;
	s_recentPresetsLoaded = true;

	VDRegistryAppKey key("Shader Presets", false);
	for (int i = 0; i < kMaxRecentPresets; i++) {
		char kn[16];
		snprintf(kn, sizeof(kn), "Recent%d", i);
		VDStringW ws;
		if (key.getString(kn, ws) && !ws.empty()) {
			std::string u8 = WToStd(ws);
			if (!u8.empty())
				s_recentPresets.push_back(std::move(u8));
		}
	}
}

static void SaveRecentPresets() {
	VDRegistryAppKey key("Shader Presets", true);
	for (int i = 0; i < kMaxRecentPresets; i++) {
		char kn[16];
		snprintf(kn, sizeof(kn), "Recent%d", i);
		if (i < (int)s_recentPresets.size())
			key.setString(kn, VDTextU8ToW(VDStringSpanA(s_recentPresets[i].c_str())).c_str());
		else
			key.setString(kn, L"");
	}
}

static void AddRecentPreset(const std::string &path) {
	s_recentPresets.erase(
		std::remove(s_recentPresets.begin(), s_recentPresets.end(), path),
		s_recentPresets.end());

	s_recentPresets.insert(s_recentPresets.begin(), path);

	if ((int)s_recentPresets.size() > kMaxRecentPresets)
		s_recentPresets.resize(kMaxRecentPresets);

	SaveRecentPresets();
}

// =========================================================================
// Settings persistence — last preset path and parameter overrides
// =========================================================================

// Derive a registry-safe key name from a preset path (use filename stem).
static std::string PresetKeyName(const std::string &presetPath) {
	const char *s = presetPath.c_str();
	const char *slash = strrchr(s, '/');
	if (!slash) slash = strrchr(s, '\\');
	const char *name = slash ? slash + 1 : s;
	// Strip extension
	std::string key(name);
	auto dot = key.rfind('.');
	if (dot != std::string::npos)
		key.erase(dot);
	return key;
}

static void SaveLastPresetPath(const std::string &path) {
	VDRegistryAppKey key("Shader Presets", true);
	key.setString("LastPreset", VDTextU8ToW(VDStringSpanA(path.c_str())).c_str());
}

static std::string LoadLastPresetPath() {
	VDRegistryAppKey key("Shader Presets", false);
	VDStringW ws;
	if (key.getString("LastPreset", ws) && !ws.empty())
		return WToStd(ws);
	return {};
}

static void SaveParameterValues(IDisplayBackend *backend) {
	if (!backend || !backend->HasShaderPreset())
		return;

	std::string presetKey = PresetKeyName(backend->GetShaderPresetPath());
	if (presetKey.empty())
		return;

	std::string regPath = "Shader Presets\\Params\\" + presetKey;
	VDRegistryAppKey key(regPath.c_str(), true);

	auto params = backend->GetShaderParameters();
	for (const auto &p : params) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%.6g", p.value);
		key.setString(p.name.c_str(), buf);
	}
}

static void RestoreParameterValues(IDisplayBackend *backend) {
	if (!backend || !backend->HasShaderPreset())
		return;

	std::string presetKey = PresetKeyName(backend->GetShaderPresetPath());
	if (presetKey.empty())
		return;

	std::string regPath = "Shader Presets\\Params\\" + presetKey;
	VDRegistryAppKey key(regPath.c_str(), false);

	auto params = backend->GetShaderParameters();
	for (const auto &p : params) {
		VDStringA vs;
		if (key.getString(p.name.c_str(), vs) && !vs.empty()) {
			float val = (float)atof(vs.c_str());
			backend->SetShaderParameter(p.name.c_str(), val);
		}
	}
}

// =========================================================================
// File dialog callback — deferred to main thread
// =========================================================================

static std::mutex s_pendingMutex;
static std::string s_pendingPresetPath;

static void ShaderPresetFileCallback(void *, const char * const *filelist, int) {
	if (filelist && filelist[0]) {
		std::lock_guard<std::mutex> lk(s_pendingMutex);
		s_pendingPresetPath = filelist[0];
	}
}

// =========================================================================
// Preset loading
// =========================================================================

static bool LoadShaderPreset(IDisplayBackend *backend, const std::string &path) {
	if (!backend || path.empty())
		return false;

	if (!backend->LoadShaderPreset(path.c_str())) {
		LOG_ERROR("ShaderPresets", "Failed to load preset: %s", path.c_str());
		return false;
	}

	AddRecentPreset(path);
	SaveLastPresetPath(path);
	RestoreParameterValues(backend);

	LOG_INFO("ShaderPresets", "Activated preset: %s", path.c_str());
	return true;
}

static void ClearShaderPreset(IDisplayBackend *backend) {
	if (!backend)
		return;

	backend->ClearShaderPreset();
	SaveLastPresetPath("");

	LOG_INFO("ShaderPresets", "Cleared shader preset — built-in effects restored");
}

// =========================================================================
// Auto-load on startup
// =========================================================================

void ATUIShaderPresetsAutoLoad(IDisplayBackend *backend) {
	LoadRecentPresets();

	std::string lastPath = LoadLastPresetPath();
	if (!lastPath.empty() && backend && backend->SupportsExternalShaders()) {
		LoadShaderPreset(backend, lastPath);
	}
}

// =========================================================================
// Poll for deferred file dialog results (call each frame from main loop)
// =========================================================================

void ATUIShaderPresetsPoll(IDisplayBackend *backend) {
	std::string path;
	{
		std::lock_guard<std::mutex> lk(s_pendingMutex);
		path.swap(s_pendingPresetPath);
	}

	if (!path.empty()) {
		LoadShaderPreset(backend, path);
	}
}

// =========================================================================
// Platform-specific help text
// =========================================================================

static const char *GetLibrashaderInstallHelp() {
#if defined(__linux__)
	return
		"librashader setup (Linux):\n\n"
		"1. Install the librashader shared library:\n"
		"   - Fedora/openSUSE: available via OBS (Open Build Service)\n"
		"   - Arch Linux: 'librashader' in AUR\n"
		"   - Other: download librashader.so from GitHub Releases\n"
		"     and place it in /usr/lib/ or /usr/local/lib/\n\n"
		"2. Download shader presets using the button below,\n"
		"   or manually download the ZIP from:\n"
		"   https://github.com/libretro/slang-shaders\n"
		"   and extract to ~/.config/altirra/shaders/slang-shaders\n\n"
		"3. Use View > Shader Preset > Browse to select a .slangp file.\n"
		"   Recommended: crt/ folder contains CRT simulation shaders.";
#elif defined(__APPLE__)
	return
		"librashader setup (macOS):\n\n"
		"1. Install the librashader shared library:\n"
		"   - Download librashader.dylib from:\n"
		"     https://github.com/SnowflakePowered/librashader/releases\n"
		"   - Place it in /usr/local/lib/\n\n"
		"2. Download shader presets using the button below,\n"
		"   or manually download the ZIP from:\n"
		"   https://github.com/libretro/slang-shaders\n"
		"   and extract to ~/Library/Application Support/altirra/shaders/\n\n"
		"3. Use View > Shader Preset > Browse to select a .slangp file.\n"
		"   Recommended: crt/ folder contains CRT simulation shaders.";
#elif defined(_WIN32)
	return
		"librashader setup (Windows):\n\n"
		"1. Install the librashader shared library:\n"
		"   - Download librashader.dll from:\n"
		"     https://github.com/SnowflakePowered/librashader/releases\n"
		"   - Place it next to AltirraSDL.exe or in your PATH.\n\n"
		"2. Download shader presets using the button below,\n"
		"   or manually download the ZIP from:\n"
		"   https://github.com/libretro/slang-shaders\n"
		"   and extract anywhere you like.\n\n"
		"3. Use View > Shader Preset > Browse to select a .slangp file.\n"
		"   Recommended: crt\\ folder contains CRT simulation shaders.";
#else
	return
		"librashader setup:\n\n"
		"1. Install librashader from:\n"
		"   https://github.com/SnowflakePowered/librashader/releases\n\n"
		"2. Download shader presets from:\n"
		"   https://github.com/libretro/slang-shaders\n\n"
		"3. Use View > Shader Preset > Browse to select a .slangp file.";
#endif
}

// =========================================================================
// Download shader pack helper
// =========================================================================

static bool s_downloadInProgress = false;
static std::string s_downloadStatus;
static std::mutex s_downloadMutex;

// Returns the default shader directory for this platform.
static std::string GetDefaultShaderDir() {
#if defined(__linux__)
	const char *home = getenv("HOME");
	if (home)
		return std::string(home) + "/.config/altirra/shaders";
#elif defined(__APPLE__)
	const char *home = getenv("HOME");
	if (home)
		return std::string(home) + "/Library/Application Support/altirra/shaders";
#elif defined(_WIN32)
	const char *appdata = getenv("LOCALAPPDATA");
	if (appdata)
		return std::string(appdata) + "\\altirra\\shaders";
#endif
	return "shaders";
}

static void SetDownloadStatus(const std::string &msg) {
	std::lock_guard<std::mutex> lk(s_downloadMutex);
	s_downloadStatus = msg;
}

// Create a directory and all parents.  Works on all platforms.
static bool CreateDirRecursive(const std::string &path) {
#if defined(_WIN32)
	std::string cmd = "cmd /c mkdir \"" + path + "\" 2>nul";
#else
	std::string cmd = "mkdir -p \"" + path + "\" 2>/dev/null";
#endif
	return system(cmd.c_str()) == 0;
}

// Check if a directory exists by trying to open a known file or stat.
static bool DirectoryExists(const std::string &path) {
#if defined(_WIN32)
	std::string cmd = "cmd /c if exist \"" + path + "\\*\" (exit 0) else (exit 1)";
	return system(cmd.c_str()) == 0;
#else
	std::string cmd = "test -d \"" + path + "\"";
	return system(cmd.c_str()) == 0;
#endif
}

static void DownloadShaderPackThread() {
	// The ZIP from GitHub extracts to "slang-shaders-master/".
	// We download, extract, and rename to "slang-shaders/".
	static const char *kZipURL =
		"https://github.com/libretro/slang-shaders/archive/refs/heads/master.zip";

	std::string destDir = GetDefaultShaderDir();
	std::string targetDir = destDir + "/slang-shaders";

	// Check if already downloaded
	if (DirectoryExists(targetDir + "/crt")) {
		SetDownloadStatus("Shaders already installed at:\n" + targetDir +
			"\n\nTo re-download, delete the folder first.");
		{
			std::lock_guard<std::mutex> lk(s_downloadMutex);
			s_downloadInProgress = false;
		}
		return;
	}

	// Create destination directory
	SetDownloadStatus("Creating directory...");
	if (!CreateDirRecursive(destDir)) {
		std::lock_guard<std::mutex> lk(s_downloadMutex);
		s_downloadStatus = "Error: could not create directory:\n" + destDir;
		s_downloadInProgress = false;
		return;
	}

	// Download the ZIP using curl (available on Linux, macOS, Windows 10+)
	std::string zipPath = destDir + "/slang-shaders.zip";
	SetDownloadStatus("Downloading slang-shaders (~60 MB compressed)...");

#if defined(_WIN32)
	std::string dlCmd = "curl -L -o \"" + zipPath + "\" \"" + kZipURL + "\" 2>nul";
#else
	std::string dlCmd = "curl -fSL -o \"" + zipPath + "\" \"" + kZipURL + "\" 2>/dev/null";
#endif

	int ret = system(dlCmd.c_str());
	if (ret != 0) {
		// Try wget as fallback (common on Linux)
		std::string wgetCmd = "wget -q -O \"" + zipPath + "\" \"" + kZipURL + "\"";
		ret = system(wgetCmd.c_str());
	}

	if (ret != 0) {
		std::lock_guard<std::mutex> lk(s_downloadMutex);
		s_downloadStatus =
			"Download failed. Please download manually:\n"
			"https://github.com/libretro/slang-shaders\n"
			"Click 'Code' > 'Download ZIP', then extract to:\n" + destDir;
		s_downloadInProgress = false;
		// Clean up partial download
		remove(zipPath.c_str());
		return;
	}

	// Extract the ZIP
	SetDownloadStatus("Extracting...");

#if defined(_WIN32)
	// Windows 10+ has tar that can handle zip files
	std::string extractCmd = "cd /d \"" + destDir + "\" && tar -xf \"" + zipPath + "\" 2>nul";
#elif defined(__APPLE__)
	// macOS always has unzip and ditto
	std::string extractCmd = "unzip -q -o \"" + zipPath + "\" -d \"" + destDir + "\" 2>/dev/null";
#else
	// Linux — try unzip first, then python3 as fallback
	std::string extractCmd = "unzip -q -o \"" + zipPath + "\" -d \"" + destDir + "\" 2>/dev/null";
#endif

	ret = system(extractCmd.c_str());

#if !defined(_WIN32) && !defined(__APPLE__)
	if (ret != 0) {
		// Fallback: use python3 zipfile (very widely available)
		std::string pyCmd = "python3 -c \"import zipfile,sys; "
			"zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])\" "
			"\"" + zipPath + "\" \"" + destDir + "\" 2>/dev/null";
		ret = system(pyCmd.c_str());
	}
#endif

	if (ret != 0) {
		std::lock_guard<std::mutex> lk(s_downloadMutex);
		s_downloadStatus =
			"Extraction failed. The ZIP was downloaded to:\n" + zipPath +
			"\n\nPlease extract it manually to:\n" + destDir;
		s_downloadInProgress = false;
		return;
	}

	// GitHub ZIP extracts to "slang-shaders-master/" — rename to "slang-shaders/"
	std::string extractedDir = destDir + "/slang-shaders-master";
	if (DirectoryExists(extractedDir)) {
#if defined(_WIN32)
		std::string renCmd = "cmd /c move \"" + extractedDir + "\" \"" + targetDir + "\" 2>nul";
#else
		std::string renCmd = "mv \"" + extractedDir + "\" \"" + targetDir + "\" 2>/dev/null";
#endif
		system(renCmd.c_str());
	}

	// Clean up the ZIP file
	remove(zipPath.c_str());

	{
		std::lock_guard<std::mutex> lk(s_downloadMutex);
		if (DirectoryExists(targetDir + "/crt"))
			s_downloadStatus = "Done! Shaders installed to:\n" + targetDir;
		else
			s_downloadStatus = "Something went wrong during extraction.\n"
				"Check the contents of:\n" + destDir;
		s_downloadInProgress = false;
	}
}

// =========================================================================
// View > Shader Preset submenu
// =========================================================================

void ATUIRenderShaderPresetMenu(IDisplayBackend *backend) {
	LoadRecentPresets();

	bool available = backend && backend->SupportsExternalShaders();
	bool hasPreset = backend && backend->HasShaderPreset();

	if (ImGui::BeginMenu("Shader Preset", available)) {
		// (None) — clear preset, restore built-in effects
		if (ImGui::MenuItem("(None)", nullptr, !hasPreset)) {
			ClearShaderPreset(backend);
		}

		// Browse... — open file dialog
		if (ImGui::MenuItem("Browse...")) {
			static const SDL_DialogFileFilter kFilters[] = {
				{ "Shader Presets (*.slangp, *.glslp)", "slangp;glslp" },
				{ "All Files (*.*)", "*" },
			};

			// Start in the default shader dir if it exists
			std::string defaultDir = GetDefaultShaderDir() + "/slang-shaders";
			const char *startDir = nullptr;
			FILE *dirTest = fopen((defaultDir + "/stock.slangp").c_str(), "r");
			if (dirTest) {
				fclose(dirTest);
				startDir = defaultDir.c_str();
			}

			SDL_ShowOpenFileDialog(ShaderPresetFileCallback, nullptr,
				g_pWindow, kFilters, 2, startDir, false);
		}

		// Recent presets
		if (!s_recentPresets.empty()) {
			ImGui::Separator();
			for (const auto &path : s_recentPresets) {
				const char *filename = path.c_str();
				const char *slash = strrchr(filename, '/');
				if (!slash) slash = strrchr(filename, '\\');
				if (slash) filename = slash + 1;

				bool isCurrent = hasPreset && backend &&
					path == backend->GetShaderPresetPath();

				if (ImGui::MenuItem(filename, nullptr, isCurrent)) {
					LoadShaderPreset(backend, path);
				}

				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
					ImGui::SetTooltip("%s", path.c_str());
			}
		}

		ImGui::EndMenu();
	}
}

// =========================================================================
// Shader Parameters panel — shown when a preset is active
// =========================================================================

void ATUIRenderShaderParameters(ATUIState &state) {
	if (!state.showShaderParams)
		return;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	if (!backend || !backend->HasShaderPreset()) {
		state.showShaderParams = false;
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Shader Parameters", &state.showShaderParams,
		ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose())
		state.showShaderParams = false;

	// Show preset name
	const char *presetPath = backend->GetShaderPresetPath();
	if (presetPath && presetPath[0]) {
		const char *name = strrchr(presetPath, '/');
		if (!name) name = strrchr(presetPath, '\\');
		if (name) name++; else name = presetPath;
		ImGui::Text("Preset: %s", name);
		ImGui::Separator();
		ImGui::Spacing();
	}

	auto params = backend->GetShaderParameters();
	if (params.empty()) {
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
			"This preset has no adjustable parameters.");
	} else {
		for (auto &p : params) {
			const char *label = p.description.empty() ? p.name.c_str() : p.description.c_str();

			bool isToggle = (p.minimum == 0.0f && p.maximum == 1.0f && p.step >= 1.0f);

			if (isToggle) {
				bool checked = (p.value >= 0.5f);
				if (ImGui::Checkbox(label, &checked)) {
					float newVal = checked ? 1.0f : 0.0f;
					backend->SetShaderParameter(p.name.c_str(), newVal);
				}
			} else {
				float val = p.value;
				ImGui::Text("%s: %.3g", label, val);
				std::string sliderId = "##" + p.name;
				if (ImGui::SliderFloat(sliderId.c_str(), &val, p.minimum, p.maximum, "")) {
					backend->SetShaderParameter(p.name.c_str(), val);
				}
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save Parameters")) {
			SaveParameterValues(backend);
		}
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(saved per preset)");
	}

	ImGui::End();
}

// =========================================================================
// Shader Setup Help dialog — platform-specific instructions + download
// =========================================================================

void ATUIRenderShaderSetupHelp(ATUIState &state) {
	if (!state.showShaderSetup)
		return;

	ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Shader Preset Setup", &state.showShaderSetup,
		ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose())
		state.showShaderSetup = false;

	// Librashader availability status
	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool libAvailable = backend && backend->SupportsExternalShaders();

	if (libAvailable) {
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
			"librashader: installed and available");
	} else {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
			"librashader: not found");
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Platform-specific setup instructions
	ImGui::TextWrapped("%s", GetLibrashaderInstallHelp());

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Download shader pack section
	ImGui::Text("Quick Download");
	ImGui::Spacing();

	std::string shaderDir = GetDefaultShaderDir() + "/slang-shaders";
	ImGui::TextWrapped("Download the official RetroArch slang-shaders collection "
		"(~60 MB download, ~180 MB extracted) to:\n%s", shaderDir.c_str());

	ImGui::Spacing();

	{
		std::lock_guard<std::mutex> lk(s_downloadMutex);

		ImGui::BeginDisabled(s_downloadInProgress);
		if (ImGui::Button("Download Shader Pack")) {
			s_downloadInProgress = true;
			s_downloadStatus = "Starting download...";
			std::thread(DownloadShaderPackThread).detach();
		}
		ImGui::EndDisabled();

		if (!s_downloadStatus.empty()) {
			ImGui::Spacing();
			if (s_downloadInProgress) {
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f),
					"%s", s_downloadStatus.c_str());
			} else if (s_downloadStatus.find("Done!") == 0 ||
					   s_downloadStatus.find("already installed") != std::string::npos) {
				ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
					"%s", s_downloadStatus.c_str());
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
					"%s", s_downloadStatus.c_str());
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextWrapped(
		"Recommended presets for Atari emulation:\n"
		"  crt/crt-lottes.slangp       - fast, good CRT look\n"
		"  crt/crt-geom.slangp         - classic CRT geometry\n"
		"  crt/crt-royale.slangp       - high quality (GPU intensive)\n"
		"  crt/crt-hyllian.slangp      - sharp CRT simulation\n"
		"  crt/crt-guest-advanced.slangp - highly configurable");

	ImGui::End();
}
