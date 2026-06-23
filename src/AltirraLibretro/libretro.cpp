#include <stdafx.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <algorithm>
#include <string>
#include <vector>

#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "libretro/libretro.h"
#include "version.h"

#include "constants.h"
#include "cpu.h"
#include "devicemanager.h"
#include "diskinterface.h"
#include "gtia.h"
#include "inputdefs.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "settings.h"
#include "simulator.h"
#include "savestateio.h"
#include "libretro_video.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>
#include <at/atcore/device.h>
#include <at/atio/atfs.h>
#include <at/atio/image.h>
#include <at/atcore/configvar.h>
#include <at/atcore/constants.h>
#include <at/atcore/media.h>
#include <at/atcore/propertyset.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <at/atnetworksockets/nativesockets.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/system/registry.h>
#include <vd2/system/text.h>
#include <vd2/system/zip.h>

class ATDeviceManager;
extern void ATRegistryLoadFromDisk();
extern void ATInitSaveStateDeserializer();
extern void ATRegisterDevices(ATDeviceManager& dm);
extern void ATRegisterDeviceXCmds(ATDeviceManager& dm);
extern void ATOptionsLoad();
extern bool ATLoadDefaultProfiles();
extern void ATInitDebugger();
extern void ATShutdownDebugger();
extern ATUIKeyboardOptions g_kbdOpts;
extern void ATLibretroSetAudioSink(size_t (*sink)(const sint16 *data, uint32 frames));
extern void ATSetConfigDirOverride(const char *path);
extern VDStringA ATGetConfigDir();

ATSimulator g_sim;

namespace {
retro_environment_t g_env = nullptr;
retro_video_refresh_t g_video = nullptr;
retro_audio_sample_t g_audioSample = nullptr;
retro_audio_sample_batch_t g_audioBatch = nullptr;
retro_input_poll_t g_inputPoll = nullptr;
retro_input_state_t g_inputState = nullptr;
unsigned g_controllerDevices[2] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD };

struct CoreState {
	bool simulatorInitialized = false;
	bool gameLoaded = false;
	IVDVideoDisplay *nullDisplay = nullptr;
	VDPixmapBuffer frameBuffer;
	std::vector<uint8_t> lastFrame;
	int lastFrameW = 0;
	int lastFrameH = 0;
	ptrdiff_t lastFramePitch = 0;
	unsigned reportedGeometryW = 0;
	unsigned reportedGeometryH = 0;
	ATVideoStandard lastStandard = kATVideoStandard_NTSC;
	bool inputBitmasksSupported = false;
	bool buttonsHeld[2][9] {};
	uint32 buttonHeldCodes[2][9] {};
	bool mouseButtonsHeld[5] {};
	std::vector<uint32> keyboardHeldCodes;
	bool keyboardCallbackEventSeen = false;
	bool consoleHeld[3] {};
	bool keyboardConsoleHeld[3] {};
	bool keyboardBreakHeld = false;
	std::vector<uint8_t> serializeCache;
	bool serializeCacheValid = false;
	size_t serializeFixedSize = 0;
	ATHardwareMode pendingHardwareMode = kATHardwareMode_800XL;
	ATMemoryMode pendingMemoryMode = kATMemoryMode_320K;
	ATVideoStandard pendingVideoStandard = kATVideoStandard_NTSC;
	ATCPUMode pendingCPUMode = kATCPUMode_6502;
	uint32 pendingCPUSubCycles = 1;
	bool pendingBasicEnabled = false;
	bool pendingStereoPokeyEnabled = false;
	bool pendingVbxeEnabled = false;
	bool pendingCovoxEnabled = false;
	bool pendingSoundBoardEnabled = false;
	bool pendingRapidusEnabled = false;
	bool optionHardwarePending = true;
	bool optionMemoryPending = true;
	bool optionVideoPending = true;
	bool optionCpuPending = true;
	bool optionBasicPending = true;
	bool optionStereoPokeyPending = true;
	bool optionVbxePending = true;
	bool optionCovoxPending = true;
	bool optionSoundBoardPending = true;
	bool optionRapidusPending = true;
	VDStringA systemDirectory;
	VDStringA saveDirectory;
	VDStringA configDirectory;
	struct DiskEntry {
		std::string path;
		std::string label;
	};
	std::vector<DiskEntry> diskImages;
	unsigned diskIndex = 0;
	bool diskEjected = false;
};

CoreState g_core;

void InvalidateSerializeCache();
void InitDefaultInputMaps();
void ReleaseInput();

constexpr const char *kValidExtensions =
	"atr|xfd|atx|atz|dcm|pro|arc|"
	"bin|rom|car|a52|"
	"xex|exe|obx|com|bas|"
	"cas|wav|flac|ogg|"
	"sap|vgm|vgz|"
	"zip|gz|"
	"altstate|atstate2|"
	"m3u";

constexpr uint8_t kStateMagic[8] = { 'A', 'L', 'T', 'R', 'L', 'R', 'S', 'T' };
constexpr uint32 kStateVersion = 1;
constexpr size_t kStateHeaderSize = 20;
constexpr size_t kStateFixedMaxSize = 64 * 1024 * 1024;

void QueryCoreDirectories() {
	if (!g_env)
		return;

	const char *dir = nullptr;
	if (g_env(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir && *dir)
		g_core.systemDirectory = dir;

	dir = nullptr;
	if (g_env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir && *dir)
		g_core.saveDirectory = dir;

	if (!g_core.saveDirectory.empty()) {
		g_core.configDirectory = g_core.saveDirectory;
		if (!g_core.configDirectory.empty()
			&& g_core.configDirectory.back() != '/'
			&& g_core.configDirectory.back() != '\\')
		{
			g_core.configDirectory += '/';
		}
		g_core.configDirectory += "Altirra";

		ATSetConfigDirOverride(g_core.configDirectory.c_str());
	}
}

VDStringW U8PathToW(const VDStringA& path) {
	return VDTextU8ToW(VDStringSpanA(path.c_str()));
}

bool IsDirectoryPath(const VDStringW& path) {
	const uint32 attrs = VDFileGetAttributes(path.c_str());
	return attrs != kVDFileAttr_Invalid && (attrs & kVDFileAttr_Directory);
}

void AddUniqueDirectory(std::vector<VDStringW>& dirs, const VDStringW& dir) {
	if (dir.empty() || !IsDirectoryPath(dir))
		return;

	if (std::find_if(dirs.begin(), dirs.end(),
		[&](const VDStringW& existing) {
			return VDFileIsPathEqual(existing.c_str(), dir.c_str());
		}) == dirs.end())
	{
		dirs.push_back(dir);
	}
}

void RegisterDetectedFirmware(ATFirmwareManager& fwm,
	const VDStringW& filePath, const vdfastvector<uint8>& data)
{
	ATFirmwareInfo detInfo {};
	ATSpecificFirmwareType specificType = kATSpecificFirmwareType_None;
	sint32 knownFirmwareIndex = -1;

	if (ATFirmwareAutodetect(data.data(), (uint32)data.size(), detInfo,
		specificType, knownFirmwareIndex) != ATFirmwareDetection::SpecificImage)
	{
		return;
	}

	const uint64 id = ATGetFirmwareIdFromPath(filePath.c_str());
	ATFirmwareInfo existing {};
	if (!fwm.GetFirmwareInfo(id, existing)) {
		ATFirmwareInfo fw {};
		fw.mId = id;
		fw.mFlags = 0;
		fw.mbVisible = true;
		fw.mbAutoselect = true;
		fw.mName = detInfo.mName.empty()
			? VDStringW(VDFileSplitPath(filePath.c_str()))
			: detInfo.mName;
		fw.mPath = filePath;
		fw.mType = detInfo.mType;
		fwm.AddFirmware(fw);
	}

	if (!fwm.GetDefaultFirmware(detInfo.mType))
		fwm.SetDefaultFirmware(detInfo.mType, id);

	if (specificType != kATSpecificFirmwareType_None
		&& !fwm.GetSpecificFirmware(specificType))
	{
		fwm.SetSpecificFirmware(specificType, id);
	}
}

void ScanFirmwareDirectory(ATFirmwareManager& fwm, const VDStringW& dir) {
	VDStringW pattern = dir;
	if (!pattern.empty() && pattern.back() != L'/' && pattern.back() != L'\\')
		pattern += L'/';
	pattern += L"*.*";

	try {
		VDDirectoryIterator it(pattern.c_str());
		while (it.Next()) {
			if (it.IsDirectory())
				continue;

			if (it.GetAttributes() & (kVDFileAttr_System | kVDFileAttr_Hidden))
				continue;

			if (!ATFirmwareAutodetectCheckSize(it.GetSize()))
				continue;

			const VDStringW filePath = it.GetFullPath();
			try {
				VDFile f(filePath.c_str());
				const sint64 sz = f.size();
				if (sz <= 0 || sz > 16 * 1024 * 1024)
					continue;

				vdfastvector<uint8> data((size_t)sz);
				f.read(data.data(), (long)sz);
				f.close();

				RegisterDetectedFirmware(fwm, filePath, data);
			} catch(...) {
			}
		}
	} catch(...) {
	}
}

void RegisterRetroArchFirmwareDirectories() {
	ATFirmwareManager *const fwm = g_sim.GetFirmwareManager();
	if (!fwm)
		return;

	std::vector<VDStringW> dirs;

	if (!g_core.systemDirectory.empty()) {
		VDStringW systemDir = U8PathToW(g_core.systemDirectory);
		AddUniqueDirectory(dirs, VDMakePath(systemDir.c_str(), L"Altirra"));
		AddUniqueDirectory(dirs, systemDir);
	}

	VDStringA configDirA = g_core.configDirectory.empty()
		? ATGetConfigDir()
		: g_core.configDirectory;
	VDStringW configDir = U8PathToW(configDirA);
	AddUniqueDirectory(dirs, VDMakePath(configDir.c_str(), L"firmware"));
	AddUniqueDirectory(dirs, configDir);

	for (const VDStringW& dir : dirs)
		ScanFirmwareDirectory(*fwm, dir);
}

std::string TrimLine(std::string s) {
	while (!s.empty() && (s.back() == '\n' || s.back() == '\r'
		|| s.back() == ' ' || s.back() == '\t'))
	{
		s.pop_back();
	}

	size_t first = 0;
	while (first < s.size() && (s[first] == ' ' || s[first] == '\t'))
		++first;

	if (first)
		s.erase(0, first);

	return s;
}

bool HasExtension(const char *path, const char *ext) {
	if (!path || !ext)
		return false;

	const char *dot = std::strrchr(path, '.');
	if (!dot || !*dot)
		return false;

	++dot;
	while (*dot && *ext) {
		const char a = *dot++;
		const char b = *ext++;
		const char la = (a >= 'A' && a <= 'Z') ? (char)(a + ('a' - 'A')) : a;
		const char lb = (b >= 'A' && b <= 'Z') ? (char)(b + ('a' - 'A')) : b;
		if (la != lb)
			return false;
	}

	return !*dot && !*ext;
}

bool IsDiskPath(const char *path) {
	static constexpr const char *kDiskExts[] = {
		"atr", "xfd", "atx", "atz", "dcm", "pro", "arc"
	};

	for (const char *ext : kDiskExts) {
		if (HasExtension(path, ext))
			return true;
	}

	return false;
}

std::string GetPathLabel(const std::string& path) {
	const char *start = path.c_str();
	const char *slash = std::strrchr(start, '/');
	const char *backslash = std::strrchr(start, '\\');
	const char *leaf = slash && backslash
		? std::max(slash, backslash) + 1
		: slash ? slash + 1 : backslash ? backslash + 1 : start;

	const char *dot = std::strrchr(leaf, '.');
	if (dot && dot != leaf)
		return std::string(leaf, dot);

	return leaf;
}

std::string ResolveRelativePath(const std::string& baseFile,
	const std::string& child)
{
	if (child.empty())
		return child;

	if (child[0] == '/' || child[0] == '\\')
		return child;

#ifdef _WIN32
	if (child.size() >= 2 && child[1] == ':')
		return child;
#endif

	const size_t slash = baseFile.find_last_of("/\\");
	if (slash == std::string::npos)
		return child;

	return baseFile.substr(0, slash + 1) + child;
}

bool LoadM3U(const char *path) {
	g_core.diskImages.clear();
	g_core.diskIndex = 0;
	g_core.diskEjected = false;

	FILE *f = std::fopen(path, "rb");
	if (!f)
		return false;

	char line[4096];
	while (std::fgets(line, sizeof line, f)) {
		std::string s = TrimLine(line);
		if (s.empty() || s[0] == '#')
			continue;

		std::string resolved = ResolveRelativePath(path, s);
		g_core.diskImages.push_back({ resolved, GetPathLabel(resolved) });
	}

	std::fclose(f);
	return !g_core.diskImages.empty();
}

bool MountDiskIndex(unsigned index) {
	if (!g_core.simulatorInitialized || index >= g_core.diskImages.size())
		return false;

	const std::string& path = g_core.diskImages[index].path;
	if (path.empty()) {
		g_sim.GetDiskInterface(0).UnloadDisk();
		return true;
	}

	ATImageLoadContext ctx {};
	ctx.mLoadIndex = 0;

	g_sim.GetDiskInterface(0).UnloadDisk();
	const VDStringW wpath = VDTextU8ToW(VDStringSpanA(path.c_str()));
	return g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx);
}

bool DiskSetEjectState(bool ejected) {
	if (!g_core.simulatorInitialized)
		return false;

	if (ejected) {
		g_sim.GetDiskInterface(0).UnloadDisk();
		g_core.diskEjected = true;
		return true;
	}

	if (g_core.diskImages.empty() || g_core.diskIndex >= g_core.diskImages.size())
		return false;

	if (!MountDiskIndex(g_core.diskIndex))
		return false;

	g_core.diskEjected = false;
	InvalidateSerializeCache();
	return true;
}

bool DiskGetEjectState() {
	return g_core.diskEjected;
}

unsigned DiskGetImageIndex() {
	return g_core.diskIndex;
}

bool DiskSetImageIndex(unsigned index) {
	if (index >= g_core.diskImages.size())
		return false;

	if (!g_core.diskEjected && !MountDiskIndex(index))
		return false;

	g_core.diskIndex = index;
	InvalidateSerializeCache();
	return true;
}

unsigned DiskGetNumImages() {
	return (unsigned)g_core.diskImages.size();
}

bool DiskReplaceImageIndex(unsigned index, const retro_game_info *info) {
	if (index >= g_core.diskImages.size())
		return false;

	std::string path;
	if (info && info->path)
		path = info->path;

	CoreState::DiskEntry oldEntry = g_core.diskImages[index];
	g_core.diskImages[index] = { path, path.empty() ? std::string() : GetPathLabel(path) };

	if (index == g_core.diskIndex && !g_core.diskEjected && !MountDiskIndex(index)) {
		g_core.diskImages[index] = oldEntry;
		return false;
	}

	InvalidateSerializeCache();
	return true;
}

bool DiskAddImageIndex() {
	g_core.diskImages.push_back({});
	return true;
}

bool DiskSetInitialImage(unsigned index, const char *path) {
	if (index >= g_core.diskImages.size())
		return false;

	g_core.diskIndex = index;
	if (path && *path)
		g_core.diskImages[index] = { path, GetPathLabel(path) };

	return true;
}

bool CopyDiskString(unsigned index, char *out, size_t len, bool label) {
	if (!out || !len || index >= g_core.diskImages.size())
		return false;

	const std::string& s = label
		? g_core.diskImages[index].label
		: g_core.diskImages[index].path;
	std::snprintf(out, len, "%s", s.c_str());
	return true;
}

bool DiskGetImagePath(unsigned index, char *path, size_t len) {
	return CopyDiskString(index, path, len, false);
}

bool DiskGetImageLabel(unsigned index, char *label, size_t len) {
	return CopyDiskString(index, label, len, true);
}

void RegisterDiskControl() {
	if (!g_env)
		return;

	static retro_disk_control_ext_callback callbacks {
		DiskSetEjectState,
		DiskGetEjectState,
		DiskGetImageIndex,
		DiskSetImageIndex,
		DiskGetNumImages,
		DiskReplaceImageIndex,
		DiskAddImageIndex,
		DiskSetInitialImage,
		DiskGetImagePath,
		DiskGetImageLabel,
	};

	g_env(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &callbacks);
}

static const retro_core_option_v2_category kOptionCategories[] = {
	{ "system", "System", "Computer, memory, video standard, CPU, and BASIC options." },
	{ "hardware", "Hardware Add-ons", "Common expansion hardware options." },
	{ "media", "Media", "Disk and cassette acceleration options." },
	{ "video", "Video", "Artifacting and display output options." },
	{ "audio", "Audio", "Audio output and filtering options." },
	{ "input", "Input", "Controller type options." },
	{ nullptr, nullptr, nullptr },
};

static const retro_core_option_value kSystemValues[] = {
	{ "800", "Atari 800" },
	{ "800xl", "Atari 800XL" },
	{ "1200xl", "Atari 1200XL" },
	{ "130xe", "Atari 130XE" },
	{ "xegs", "Atari XEGS" },
	{ "5200", "Atari 5200" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kMemoryValues[] = {
	{ "8K", "8K" },
	{ "16K", "16K" },
	{ "24K", "24K" },
	{ "32K", "32K" },
	{ "40K", "40K" },
	{ "48K", "48K" },
	{ "52K", "52K" },
	{ "64K", "64K" },
	{ "128K", "128K" },
	{ "256K", "256K" },
	{ "320K", "320K" },
	{ "320K_Compy", "320K Compy" },
	{ "576K", "576K" },
	{ "576K_Compy", "576K Compy" },
	{ "1088K", "1088K" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kVideoStandardValues[] = {
	{ "ntsc", "NTSC" },
	{ "pal", "PAL" },
	{ "secam", "SECAM" },
	{ "ntsc50", "NTSC 50Hz" },
	{ "pal60", "PAL 60Hz" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kDisabledEnabledValues[] = {
	{ "disabled", "Disabled" },
	{ "enabled", "Enabled" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kEnabledDisabledValues[] = {
	{ "enabled", "Enabled" },
	{ "disabled", "Disabled" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kCPUValues[] = {
	{ "6502c", "6502C" },
	{ "65c02", "65C02" },
	{ "65c816_7mhz", "65C816 7MHz" },
	{ "65c816_21mhz", "65C816 21MHz" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kSioPatchValues[] = {
	{ "off", "Off" },
	{ "disk", "Disk" },
	{ "cassette", "Cassette" },
	{ "disk_and_cassette", "Disk and Cassette" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kArtifactingValues[] = {
	{ "none", "None" },
	{ "ntsc", "NTSC" },
	{ "ntschi", "NTSC High" },
	{ "pal", "PAL" },
	{ "palhi", "PAL High" },
	{ "auto", "Auto" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kOverscanValues[] = {
	{ "normal", "Normal" },
	{ "off", "Off" },
	{ "extended", "Extended" },
	{ "full", "Full" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kInputPort1Values[] = {
	{ "joystick", "Joystick" },
	{ "5200_controller", "5200 Controller" },
	{ "paddle_a", "Paddle A" },
	{ "paddle_b", "Paddle B" },
	{ "st_mouse", "ST Mouse" },
	{ "light_pen", "Light Pen" },
	{ "light_gun", "Light Gun" },
	{ "none", "None" },
	{ nullptr, nullptr },
};

static const retro_core_option_value kInputPort2Values[] = {
	{ "none", "None" },
	{ "joystick", "Joystick" },
	{ "paddle_a", "Paddle A" },
	{ "paddle_b", "Paddle B" },
	{ "st_mouse", "ST Mouse" },
	{ nullptr, nullptr },
};

static const retro_core_option_v2_definition kOptionDefinitions[] = {
	{
		"altirra_system", "System", nullptr,
		"Selects the emulated Atari computer or console model.",
		nullptr, "system", kSystemValues, "800xl"
	},
	{
		"altirra_memory", "Memory Size", nullptr,
		"Selects the RAM expansion mode.",
		nullptr, "system", kMemoryValues, "320K"
	},
	{
		"altirra_video_standard", "Video Standard", nullptr,
		"Selects the machine video timing standard.",
		nullptr, "system", kVideoStandardValues, "ntsc"
	},
	{
		"altirra_basic", "BASIC", nullptr,
		"Enables or disables internal BASIC where supported.",
		nullptr, "system", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_cpu", "CPU", nullptr,
		"Selects the emulated CPU type and speed.",
		nullptr, "system", kCPUValues, "6502c"
	},
	{
		"altirra_illegal_instructions", "Illegal Instructions", nullptr,
		"Enables undocumented 6502 opcodes.",
		nullptr, "system", kEnabledDisabledValues, "enabled"
	},
	{
		"altirra_random_launch_delay", "Randomize Launch Delay", nullptr,
		"Adds a small random launch delay for directly loaded programs so hardware RNG state varies between runs.",
		nullptr, "system", kEnabledDisabledValues, "enabled"
	},
	{
		"altirra_randomize_exe_memory", "Randomize EXE Memory", nullptr,
		"Fills uninitialized RAM with random bytes before directly loaded programs start.",
		nullptr, "system", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_stereo_pokey", "Stereo POKEY", nullptr,
		"Enables a second POKEY for dual-chip stereo audio software.",
		nullptr, "hardware", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_vbxe", "VideoBoard XE (VBXE)", nullptr,
		"Enables VBXE 1.26 at the default $D6xx address range.",
		nullptr, "hardware", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_covox", "Covox", nullptr,
		"Enables a four-channel Covox DAC at $D600-D6FF.",
		nullptr, "hardware", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_soundboard", "SoundBoard", nullptr,
		"Enables SoundBoard 1.2 at the default $D2C0 base address.",
		nullptr, "hardware", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_rapidus", "Rapidus Accelerator", nullptr,
		"Enables the Rapidus accelerator device.",
		nullptr, "hardware", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_sio_patch", "SIO Patch", nullptr,
		"Controls disk and cassette SIO acceleration patches.",
		nullptr, "media", kSioPatchValues, "disk_and_cassette"
	},
	{
		"altirra_artifacting", "Artifacting", nullptr,
		"Selects NTSC/PAL artifact color simulation.",
		nullptr, "video", kArtifactingValues, "auto"
	},
	{
		"altirra_crop_overscan", "Crop Overscan", nullptr,
		"Selects the video crop mode.",
		nullptr, "video", kOverscanValues, "normal"
	},
	{
		"altirra_audio_filters", "Audio Filters", nullptr,
		"Enables Altirra's audio filter chain.",
		nullptr, "audio", kEnabledDisabledValues, "enabled"
	},
	{
		"altirra_stereo_as_mono", "Downmix Stereo to Mono", nullptr,
		"Mixes stereo POKEY output down to mono while keeping dual POKEY emulation enabled.",
		nullptr, "audio", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_drive_sounds", "Drive Sounds", nullptr,
		"Enables disk drive mechanical sound effects.",
		nullptr, "audio", kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_input_port1", "Input Port 1", nullptr,
		"Selects the first controller port type.",
		nullptr, "input", kInputPort1Values, "joystick"
	},
	{
		"altirra_input_port2", "Input Port 2", nullptr,
		"Selects the second controller port type.",
		nullptr, "input", kInputPort2Values, "none"
	},
	{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
};

static const retro_core_options_v2 kOptionsV2 = {
	kOptionCategories,
	kOptionDefinitions
};

static const retro_core_options_v2_intl kOptionsV2Intl = {
	&kOptionsV2,
	nullptr
};

static const retro_core_option_definition kOptionDefinitionsV1[] = {
	{
		"altirra_system", "System",
		"Selects the emulated Atari computer or console model.",
		kSystemValues, "800xl"
	},
	{
		"altirra_memory", "Memory Size",
		"Selects the RAM expansion mode.",
		kMemoryValues, "320K"
	},
	{
		"altirra_video_standard", "Video Standard",
		"Selects the machine video timing standard.",
		kVideoStandardValues, "ntsc"
	},
	{
		"altirra_basic", "BASIC",
		"Enables or disables internal BASIC where supported.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_cpu", "CPU",
		"Selects the emulated CPU type and speed.",
		kCPUValues, "6502c"
	},
	{
		"altirra_illegal_instructions", "Illegal Instructions",
		"Enables undocumented 6502 opcodes.",
		kEnabledDisabledValues, "enabled"
	},
	{
		"altirra_random_launch_delay", "Randomize Launch Delay",
		"Adds a small random launch delay for directly loaded programs so hardware RNG state varies between runs.",
		kEnabledDisabledValues, "enabled"
	},
	{
		"altirra_randomize_exe_memory", "Randomize EXE Memory",
		"Fills uninitialized RAM with random bytes before directly loaded programs start.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_stereo_pokey", "Stereo POKEY",
		"Enables a second POKEY for dual-chip stereo audio software.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_vbxe", "VideoBoard XE (VBXE)",
		"Enables VBXE 1.26 at the default $D6xx address range.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_covox", "Covox",
		"Enables a four-channel Covox DAC at $D600-D6FF.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_soundboard", "SoundBoard",
		"Enables SoundBoard 1.2 at the default $D2C0 base address.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_rapidus", "Rapidus Accelerator",
		"Enables the Rapidus accelerator device.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_sio_patch", "SIO Patch",
		"Controls disk and cassette SIO acceleration patches.",
		kSioPatchValues, "disk_and_cassette"
	},
	{
		"altirra_artifacting", "Artifacting",
		"Selects NTSC/PAL artifact color simulation.",
		kArtifactingValues, "auto"
	},
	{
		"altirra_crop_overscan", "Crop Overscan",
		"Selects the video crop mode.",
		kOverscanValues, "normal"
	},
	{
		"altirra_audio_filters", "Audio Filters",
		"Enables Altirra's audio filter chain.",
		kEnabledDisabledValues, "enabled"
	},
	{
		"altirra_stereo_as_mono", "Downmix Stereo to Mono",
		"Mixes stereo POKEY output down to mono while keeping dual POKEY emulation enabled.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_drive_sounds", "Drive Sounds",
		"Enables disk drive mechanical sound effects.",
		kDisabledEnabledValues, "disabled"
	},
	{
		"altirra_input_port1", "Input Port 1",
		"Selects the first controller port type.",
		kInputPort1Values, "joystick"
	},
	{
		"altirra_input_port2", "Input Port 2",
		"Selects the second controller port type.",
		kInputPort2Values, "none"
	},
	{ nullptr, nullptr, nullptr, nullptr, nullptr },
};

static const retro_variable kOptionVariables[] = {
	{ "altirra_system", "System; 800xl|800|1200xl|130xe|xegs|5200" },
	{ "altirra_memory", "Memory Size; 320K|8K|16K|24K|32K|40K|48K|52K|64K|128K|256K|320K_Compy|576K|576K_Compy|1088K" },
	{ "altirra_video_standard", "Video Standard; ntsc|pal|secam|ntsc50|pal60" },
	{ "altirra_basic", "BASIC; disabled|enabled" },
	{ "altirra_cpu", "CPU; 6502c|65c02|65c816_7mhz|65c816_21mhz" },
	{ "altirra_illegal_instructions", "Illegal Instructions; enabled|disabled" },
	{ "altirra_random_launch_delay", "Randomize Launch Delay; enabled|disabled" },
	{ "altirra_randomize_exe_memory", "Randomize EXE Memory; disabled|enabled" },
	{ "altirra_stereo_pokey", "Stereo POKEY; disabled|enabled" },
	{ "altirra_vbxe", "VideoBoard XE (VBXE); disabled|enabled" },
	{ "altirra_covox", "Covox; disabled|enabled" },
	{ "altirra_soundboard", "SoundBoard; disabled|enabled" },
	{ "altirra_rapidus", "Rapidus Accelerator; disabled|enabled" },
	{ "altirra_sio_patch", "SIO Patch; disk_and_cassette|off|disk|cassette" },
	{ "altirra_artifacting", "Artifacting; auto|none|ntsc|ntschi|pal|palhi" },
	{ "altirra_crop_overscan", "Crop Overscan; normal|off|extended|full" },
	{ "altirra_audio_filters", "Audio Filters; enabled|disabled" },
	{ "altirra_stereo_as_mono", "Downmix Stereo to Mono; disabled|enabled" },
	{ "altirra_drive_sounds", "Drive Sounds; disabled|enabled" },
	{ "altirra_input_port1", "Input Port 1; joystick|5200_controller|paddle_a|paddle_b|st_mouse|light_pen|light_gun|none" },
	{ "altirra_input_port2", "Input Port 2; none|joystick|paddle_a|paddle_b|st_mouse" },
	{ nullptr, nullptr },
};

const retro_core_option_v2_definition *FindOptionDefinition(const char *key) {
	for(const auto *def = kOptionDefinitions; def->key; ++def) {
		if (!std::strcmp(def->key, key))
			return def;
	}

	return nullptr;
}

const char *GetOptionValue(const char *key) {
	if (!g_env)
		return nullptr;

	retro_variable var {};
	var.key = key;

	if (g_env(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		return var.value;

	const retro_core_option_v2_definition *def = FindOptionDefinition(key);
	return def ? def->default_value : nullptr;
}

bool OptionEquals(const char *key, const char *value) {
	const char *opt = GetOptionValue(key);
	return opt && !std::strcmp(opt, value);
}

bool OptionEnabled(const char *key) {
	return OptionEquals(key, "enabled");
}

void RegisterCoreOptions() {
	if (!g_env)
		return;

	unsigned version = 0;
	if (g_env(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2) {
		if (g_env(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL, (void *)&kOptionsV2Intl))
			return;

		if (g_env(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void *)&kOptionsV2))
			return;
	}

	if (version >= 1
		&& g_env(RETRO_ENVIRONMENT_SET_CORE_OPTIONS,
			(void *)kOptionDefinitionsV1))
	{
		return;
	}

	g_env(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)kOptionVariables);
}

void QueryInputBitmaskSupport() {
	if (!g_env)
		return;

	bool supported = false;
	g_core.inputBitmasksSupported =
		g_env(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, &supported) && supported;
}

ATHardwareMode ParseHardwareMode() {
	if (OptionEquals("altirra_system", "800"))
		return kATHardwareMode_800;
	if (OptionEquals("altirra_system", "1200xl"))
		return kATHardwareMode_1200XL;
	if (OptionEquals("altirra_system", "130xe"))
		return kATHardwareMode_130XE;
	if (OptionEquals("altirra_system", "xegs"))
		return kATHardwareMode_XEGS;
	if (OptionEquals("altirra_system", "5200"))
		return kATHardwareMode_5200;

	return kATHardwareMode_800XL;
}

ATMemoryMode ParseMemoryMode() {
	if (OptionEquals("altirra_memory", "8K"))
		return kATMemoryMode_8K;
	if (OptionEquals("altirra_memory", "16K"))
		return kATMemoryMode_16K;
	if (OptionEquals("altirra_memory", "24K"))
		return kATMemoryMode_24K;
	if (OptionEquals("altirra_memory", "32K"))
		return kATMemoryMode_32K;
	if (OptionEquals("altirra_memory", "40K"))
		return kATMemoryMode_40K;
	if (OptionEquals("altirra_memory", "48K"))
		return kATMemoryMode_48K;
	if (OptionEquals("altirra_memory", "52K"))
		return kATMemoryMode_52K;
	if (OptionEquals("altirra_memory", "64K"))
		return kATMemoryMode_64K;
	if (OptionEquals("altirra_memory", "128K"))
		return kATMemoryMode_128K;
	if (OptionEquals("altirra_memory", "256K"))
		return kATMemoryMode_256K;
	if (OptionEquals("altirra_memory", "320K_Compy"))
		return kATMemoryMode_320K_Compy;
	if (OptionEquals("altirra_memory", "576K"))
		return kATMemoryMode_576K;
	if (OptionEquals("altirra_memory", "576K_Compy"))
		return kATMemoryMode_576K_Compy;
	if (OptionEquals("altirra_memory", "1088K"))
		return kATMemoryMode_1088K;

	return kATMemoryMode_320K;
}

ATVideoStandard ParseVideoStandard() {
	if (OptionEquals("altirra_video_standard", "pal"))
		return kATVideoStandard_PAL;
	if (OptionEquals("altirra_video_standard", "secam"))
		return kATVideoStandard_SECAM;
	if (OptionEquals("altirra_video_standard", "ntsc50"))
		return kATVideoStandard_NTSC50;
	if (OptionEquals("altirra_video_standard", "pal60"))
		return kATVideoStandard_PAL60;

	return kATVideoStandard_NTSC;
}

void ParseCPUMode(ATCPUMode& mode, uint32& subCycles) {
	if (OptionEquals("altirra_cpu", "65c02")) {
		mode = kATCPUMode_65C02;
		subCycles = 1;
		return;
	}

	if (OptionEquals("altirra_cpu", "65c816_7mhz")) {
		mode = kATCPUMode_65C816;
		subCycles = 4;
		return;
	}

	if (OptionEquals("altirra_cpu", "65c816_21mhz")) {
		mode = kATCPUMode_65C816;
		subCycles = 12;
		return;
	}

	mode = kATCPUMode_6502;
	subCycles = 1;
}

ATArtifactMode ParseArtifactMode() {
	if (OptionEquals("altirra_artifacting", "none"))
		return ATArtifactMode::None;
	if (OptionEquals("altirra_artifacting", "ntsc"))
		return ATArtifactMode::NTSC;
	if (OptionEquals("altirra_artifacting", "ntschi"))
		return ATArtifactMode::NTSCHi;
	if (OptionEquals("altirra_artifacting", "pal"))
		return ATArtifactMode::PAL;
	if (OptionEquals("altirra_artifacting", "palhi"))
		return ATArtifactMode::PALHi;

	return ATArtifactMode::Auto;
}

ATGTIAEmulator::OverscanMode ParseOverscanMode() {
	if (OptionEquals("altirra_crop_overscan", "off"))
		return ATGTIAEmulator::kOverscanFull;
	if (OptionEquals("altirra_crop_overscan", "extended"))
		return ATGTIAEmulator::kOverscanExtended;
	if (OptionEquals("altirra_crop_overscan", "full"))
		return ATGTIAEmulator::kOverscanFull;

	return ATGTIAEmulator::kOverscanNormal;
}

bool SetDeviceEnabled(const char *tag, bool enabled,
	void (*setDefaults)(ATPropertySet&))
{
	ATDeviceManager *dm = g_sim.GetDeviceManager();
	if (!dm)
		return false;

	IATDevice *existing = dm->GetDeviceByTag(tag);
	const bool present = existing != nullptr;
	if (present == enabled)
		return false;

	if (enabled) {
		ATPropertySet pset;
		if (setDefaults)
			setDefaults(pset);

		try {
			return dm->AddDevice(tag, pset, false) != nullptr;
		} catch(...) {
			return false;
		}
	}

	dm->RemoveDevice(existing);
	return true;
}

bool ApplyPendingDeviceOption(bool force, bool pending, const char *tag,
	bool enabled, void (*setDefaults)(ATPropertySet&))
{
	if (!force && !pending)
		return false;

	return SetDeviceEnabled(tag, enabled, setDefaults);
}

void ReadResetOptions() {
	g_core.pendingHardwareMode = ParseHardwareMode();
	g_core.pendingMemoryMode = ParseMemoryMode();
	g_core.pendingVideoStandard = ParseVideoStandard();
	ParseCPUMode(g_core.pendingCPUMode, g_core.pendingCPUSubCycles);
	g_core.pendingBasicEnabled = OptionEquals("altirra_basic", "enabled");
	g_core.pendingStereoPokeyEnabled = OptionEnabled("altirra_stereo_pokey");
	g_core.pendingVbxeEnabled = OptionEnabled("altirra_vbxe");
	g_core.pendingCovoxEnabled = OptionEnabled("altirra_covox");
	g_core.pendingSoundBoardEnabled = OptionEnabled("altirra_soundboard");
	g_core.pendingRapidusEnabled = OptionEnabled("altirra_rapidus");
}

bool ApplyPendingResetOptions(bool force) {
	bool resetRequired = false;

	if ((force || g_core.optionHardwarePending)
		&& g_sim.GetHardwareMode() != g_core.pendingHardwareMode) {
		g_sim.SetHardwareMode(g_core.pendingHardwareMode);
		resetRequired = true;
	}

	if ((force || g_core.optionMemoryPending)
		&& g_sim.GetMemoryMode() != g_core.pendingMemoryMode) {
		g_sim.SetMemoryMode(g_core.pendingMemoryMode);
		resetRequired = true;
	}

	if ((force || g_core.optionVideoPending)
		&& g_sim.GetVideoStandard() != g_core.pendingVideoStandard) {
		g_sim.SetVideoStandard(g_core.pendingVideoStandard);
		resetRequired = true;
	}

	if ((force || g_core.optionCpuPending)
		&& (g_sim.GetCPUMode() != g_core.pendingCPUMode
			|| g_sim.GetCPUSubCycles() != g_core.pendingCPUSubCycles)) {
		const bool chipChanged =
			!g_sim.IsCPUModeOverridden()
			&& g_sim.GetCPUMode() != g_core.pendingCPUMode;
		g_sim.SetCPUMode(g_core.pendingCPUMode, g_core.pendingCPUSubCycles);
		if (chipChanged)
			resetRequired = true;
	}

	if ((force || g_core.optionBasicPending)
		&& g_sim.IsBASICEnabled() != g_core.pendingBasicEnabled) {
		g_sim.SetBASICEnabled(g_core.pendingBasicEnabled);
		resetRequired = true;
	}

	if ((force || g_core.optionStereoPokeyPending)
		&& g_sim.IsDualPokeysEnabled() != g_core.pendingStereoPokeyEnabled) {
		g_sim.SetDualPokeysEnabled(g_core.pendingStereoPokeyEnabled);
		resetRequired = true;
	}

	g_core.optionHardwarePending = false;
	g_core.optionMemoryPending = false;
	g_core.optionVideoPending = false;
	g_core.optionCpuPending = false;
	g_core.optionBasicPending = false;
	g_core.optionStereoPokeyPending = false;

	resetRequired |= ApplyPendingDeviceOption(force,
		g_core.optionVbxePending, "vbxe", g_core.pendingVbxeEnabled,
		[](ATPropertySet& p) { p.SetUint32("version", 126); });

	resetRequired |= ApplyPendingDeviceOption(force,
		g_core.optionCovoxPending, "covox", g_core.pendingCovoxEnabled,
		[](ATPropertySet& p) {
			p.SetUint32("base", 0xD600);
			p.SetUint32("size", 0x100);
			p.SetUint32("channels", 4);
		});

	resetRequired |= ApplyPendingDeviceOption(force,
		g_core.optionSoundBoardPending, "soundboard",
		g_core.pendingSoundBoardEnabled,
		[](ATPropertySet& p) {
			p.SetUint32("version", 120);
			p.SetUint32("base", 0xD2C0);
		});

	resetRequired |= ApplyPendingDeviceOption(force,
		g_core.optionRapidusPending, "rapidus",
		g_core.pendingRapidusEnabled, nullptr);

	g_core.optionVbxePending = false;
	g_core.optionCovoxPending = false;
	g_core.optionSoundBoardPending = false;
	g_core.optionRapidusPending = false;

	return resetRequired;
}

void ApplyLiveOptions() {
	if (OptionEquals("altirra_sio_patch", "off")) {
		g_sim.SetDiskSIOPatchEnabled(false);
		g_sim.SetCassetteSIOPatchEnabled(false);
	} else if (OptionEquals("altirra_sio_patch", "disk")) {
		g_sim.SetDiskSIOPatchEnabled(true);
		g_sim.SetCassetteSIOPatchEnabled(false);
	} else if (OptionEquals("altirra_sio_patch", "cassette")) {
		g_sim.SetDiskSIOPatchEnabled(false);
		g_sim.SetCassetteSIOPatchEnabled(true);
	} else {
		g_sim.SetDiskSIOPatchEnabled(true);
		g_sim.SetCassetteSIOPatchEnabled(true);
	}

	g_sim.GetGTIA().SetArtifactingMode(ParseArtifactMode());
	g_sim.GetGTIA().SetOverscanMode(ParseOverscanMode());

	if (IATAudioOutput *audio = g_sim.GetAudioOutput())
		audio->SetFiltersEnabled(!OptionEquals("altirra_audio_filters", "disabled"));

	g_sim.GetCPU().SetIllegalInsnsEnabled(
		OptionEnabled("altirra_illegal_instructions"));
	g_sim.SetRandomProgramLaunchDelayEnabled(
		OptionEnabled("altirra_random_launch_delay"));
	g_sim.SetRandomFillEXEEnabled(
		OptionEnabled("altirra_randomize_exe_memory"));
	g_sim.GetPokey().SetStereoAsMonoEnabled(
		OptionEnabled("altirra_stereo_as_mono"));
	ATUISetDriveSoundsEnabled(OptionEnabled("altirra_drive_sounds"));

	ReleaseInput();
	InitDefaultInputMaps();
}

void ReadCoreOptions() {
	const ATHardwareMode hardwareMode = ParseHardwareMode();
	const ATMemoryMode memoryMode = ParseMemoryMode();
	const ATVideoStandard videoStandard = ParseVideoStandard();
	ATCPUMode cpuMode = kATCPUMode_6502;
	uint32 cpuSubCycles = 1;
	ParseCPUMode(cpuMode, cpuSubCycles);
	const bool basicEnabled = OptionEquals("altirra_basic", "enabled");
	const bool stereoPokeyEnabled = OptionEnabled("altirra_stereo_pokey");
	const bool vbxeEnabled = OptionEnabled("altirra_vbxe");
	const bool covoxEnabled = OptionEnabled("altirra_covox");
	const bool soundBoardEnabled = OptionEnabled("altirra_soundboard");
	const bool rapidusEnabled = OptionEnabled("altirra_rapidus");

	if (hardwareMode != g_core.pendingHardwareMode) {
		g_core.pendingHardwareMode = hardwareMode;
		g_core.optionHardwarePending = true;
	}

	if (memoryMode != g_core.pendingMemoryMode) {
		g_core.pendingMemoryMode = memoryMode;
		g_core.optionMemoryPending = true;
	}

	if (videoStandard != g_core.pendingVideoStandard) {
		g_core.pendingVideoStandard = videoStandard;
		g_core.optionVideoPending = true;
	}

	if (cpuMode != g_core.pendingCPUMode
		|| cpuSubCycles != g_core.pendingCPUSubCycles) {
		g_core.pendingCPUMode = cpuMode;
		g_core.pendingCPUSubCycles = cpuSubCycles;
		g_core.optionCpuPending = true;
	}

	if (basicEnabled != g_core.pendingBasicEnabled) {
		g_core.pendingBasicEnabled = basicEnabled;
		g_core.optionBasicPending = true;
	}

	if (stereoPokeyEnabled != g_core.pendingStereoPokeyEnabled) {
		g_core.pendingStereoPokeyEnabled = stereoPokeyEnabled;
		g_core.optionStereoPokeyPending = true;
	}

	if (vbxeEnabled != g_core.pendingVbxeEnabled) {
		g_core.pendingVbxeEnabled = vbxeEnabled;
		g_core.optionVbxePending = true;
	}

	if (covoxEnabled != g_core.pendingCovoxEnabled) {
		g_core.pendingCovoxEnabled = covoxEnabled;
		g_core.optionCovoxPending = true;
	}

	if (soundBoardEnabled != g_core.pendingSoundBoardEnabled) {
		g_core.pendingSoundBoardEnabled = soundBoardEnabled;
		g_core.optionSoundBoardPending = true;
	}

	if (rapidusEnabled != g_core.pendingRapidusEnabled) {
		g_core.pendingRapidusEnabled = rapidusEnabled;
		g_core.optionRapidusPending = true;
	}

	ApplyLiveOptions();
}

struct ButtonMap {
	unsigned retroId;
	uint32 joystickCode;
	uint32 controller5200Code;
};

constexpr ButtonMap kRetropadButtonMap[] = {
	{ RETRO_DEVICE_ID_JOYPAD_LEFT, kATInputCode_JoyStick1Left, kATInputCode_JoyPOVLeft },
	{ RETRO_DEVICE_ID_JOYPAD_RIGHT, kATInputCode_JoyStick1Right, kATInputCode_JoyPOVRight },
	{ RETRO_DEVICE_ID_JOYPAD_UP, kATInputCode_JoyStick1Up, kATInputCode_JoyPOVUp },
	{ RETRO_DEVICE_ID_JOYPAD_DOWN, kATInputCode_JoyStick1Down, kATInputCode_JoyPOVDown },
	{ RETRO_DEVICE_ID_JOYPAD_B, kATInputCode_JoyButton0, kATInputCode_JoyButton0 },
	{ RETRO_DEVICE_ID_JOYPAD_A, kATInputCode_JoyButton0, kATInputCode_JoyButton0 + 1 },
	{ RETRO_DEVICE_ID_JOYPAD_START, 0, kATInputCode_JoyButton0 + 7 },
	{ RETRO_DEVICE_ID_JOYPAD_SELECT, 0, kATInputCode_JoyButton0 + 3 },
	{ RETRO_DEVICE_ID_JOYPAD_L, 0, kATInputCode_JoyButton0 + 6 },
};

constexpr unsigned kConsoleRetroIds[] = {
	RETRO_DEVICE_ID_JOYPAD_START,
	RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_L,
};

constexpr uint8 kConsoleSwitchBits[] = {
	0x01,
	0x02,
	0x04,
};

bool IsJoystickPortEnabled(unsigned port) {
	if (port == 0)
		return OptionEquals("altirra_input_port1", "joystick");

	if (port == 1)
		return OptionEquals("altirra_input_port2", "joystick");

	return false;
}

bool Is5200PortEnabled(unsigned port) {
	return port == 0 && OptionEquals("altirra_input_port1", "5200_controller");
}

bool IsPaddlePortEnabled(unsigned port) {
	if (port < std::size(g_controllerDevices)
		&& (g_controllerDevices[port] & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG)
	{
		return true;
	}

	if (port == 0)
		return OptionEquals("altirra_input_port1", "paddle_a")
			|| OptionEquals("altirra_input_port1", "paddle_b");

	if (port == 1)
		return OptionEquals("altirra_input_port2", "paddle_a")
			|| OptionEquals("altirra_input_port2", "paddle_b");

	return false;
}

bool IsSTMousePortEnabled(unsigned port) {
	if (port < std::size(g_controllerDevices)
		&& (g_controllerDevices[port] & RETRO_DEVICE_MASK) == RETRO_DEVICE_MOUSE)
	{
		return true;
	}

	if (port == 0)
		return OptionEquals("altirra_input_port1", "st_mouse");

	if (port == 1)
		return OptionEquals("altirra_input_port2", "st_mouse");

	return false;
}

bool IsLightPenPortEnabled(unsigned port) {
	if (port != 0)
		return false;

	return OptionEquals("altirra_input_port1", "light_pen");
}

bool IsLightGunPortEnabled(unsigned port) {
	if (port != 0)
		return false;

	return ((g_controllerDevices[port] & RETRO_DEVICE_MASK) == RETRO_DEVICE_LIGHTGUN)
		|| OptionEquals("altirra_input_port1", "light_gun");
}

bool IsAbsolutePointerPortEnabled(unsigned port) {
	return IsLightPenPortEnabled(port) || IsLightGunPortEnabled(port);
}

unsigned GetPaddleIndexForPort(unsigned port) {
	const bool second = (port == 0)
		? OptionEquals("altirra_input_port1", "paddle_b")
		: OptionEquals("altirra_input_port2", "paddle_b");

	return port * 2 + (second ? 1 : 0);
}

uint32 GetRetropadInputCode(unsigned port, const ButtonMap& map) {
	if (Is5200PortEnabled(port))
		return map.controller5200Code;

	if (IsPaddlePortEnabled(port)) {
		if (map.retroId == RETRO_DEVICE_ID_JOYPAD_B
			|| map.retroId == RETRO_DEVICE_ID_JOYPAD_A)
		{
			return kATInputCode_JoyButton0;
		}

		if (map.retroId == RETRO_DEVICE_ID_JOYPAD_LEFT)
			return kATInputCode_JoyPOVLeft;

		if (map.retroId == RETRO_DEVICE_ID_JOYPAD_RIGHT)
			return kATInputCode_JoyPOVRight;

		return 0;
	}

	if (IsJoystickPortEnabled(port))
		return map.joystickCode;

	return 0;
}

uint16 GetRetropadStateMask(unsigned port) {
	if (!g_inputState)
		return 0;

	if (g_core.inputBitmasksSupported) {
		return (uint16)g_inputState(
			port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
	}

	uint16 mask = 0;
	for(size_t i = 0; i < std::size(kRetropadButtonMap); ++i) {
		const unsigned id = kRetropadButtonMap[i].retroId;
		if (g_inputState(port, RETRO_DEVICE_JOYPAD, 0, id))
			mask |= (uint16)(1U << id);
	}

	return mask;
}

void AddLibretroPaddleMap(ATInputManager& im, unsigned port) {
	vdrefptr<ATInputMap> map(new ATInputMap);
	const unsigned paddleIndex = GetPaddleIndexForPort(port);
	const unsigned controller = map->AddController(
		kATInputControllerType_Paddle, paddleIndex);

	VDStringW name;
	name.sprintf(L"Libretro Paddle %c (port %u)",
		(paddleIndex & 1) ? L'B' : L'A', port + 1);
	map->SetName(name.c_str());
	map->SetQuickMap(true);
	map->SetSpecificInputUnit((int)port);
	map->AddMapping(kATInputCode_JoyButton0, controller,
		kATInputTrigger_Button0);
	map->AddMapping(kATInputCode_JoyHoriz1, controller,
		kATInputTrigger_Axis0);
	map->AddMapping(kATInputCode_JoyPOVLeft, controller,
		kATInputTrigger_Left | kATInputTriggerMode_Relative
			| (5 << kATInputTriggerSpeed_Shift));
	map->AddMapping(kATInputCode_JoyPOVRight, controller,
		kATInputTrigger_Right | kATInputTriggerMode_Relative
			| (5 << kATInputTriggerSpeed_Shift));

	im.AddInputMap(map);
	im.ActivateInputMap(map, true);
}

uint32 GetMouseButtonInputCode(size_t index) {
	switch(index) {
		case 0: return kATInputCode_MouseLMB;
		case 1: return kATInputCode_MouseRMB;
		case 2: return kATInputCode_MouseMMB;
		case 3: return kATInputCode_MouseX1B;
		case 4: return kATInputCode_MouseX2B;
		default: return 0;
	}
}

uint32 MapRetroKeyToInputCode(unsigned keycode) {
	if (keycode >= RETROK_0 && keycode <= RETROK_9)
		return kATInputCode_Key0 + (keycode - RETROK_0);

	if (keycode >= RETROK_a && keycode <= RETROK_z)
		return kATInputCode_KeyA + (keycode - RETROK_a);

	if (keycode >= RETROK_F1 && keycode <= RETROK_F12)
		return kATInputCode_KeyF1 + (keycode - RETROK_F1);

	if (keycode >= RETROK_KP0 && keycode <= RETROK_KP9)
		return kATInputCode_KeyNumpad0 + (keycode - RETROK_KP0);

	switch(keycode) {
		case RETROK_BACKSPACE: return kATInputCode_KeyBack;
		case RETROK_TAB: return kATInputCode_KeyTab;
		case RETROK_RETURN: return kATInputCode_KeyReturn;
		case RETROK_ESCAPE: return kATInputCode_KeyEscape;
		case RETROK_SPACE: return kATInputCode_KeySpace;
		case RETROK_DELETE: return kATInputCode_KeyDelete;
		case RETROK_INSERT: return kATInputCode_KeyInsert;
		case RETROK_HOME: return kATInputCode_KeyHome;
		case RETROK_END: return kATInputCode_KeyEnd;
		case RETROK_PAGEUP: return kATInputCode_KeyPrior;
		case RETROK_PAGEDOWN: return kATInputCode_KeyNext;
		case RETROK_LEFT: return kATInputCode_KeyLeft;
		case RETROK_RIGHT: return kATInputCode_KeyRight;
		case RETROK_UP: return kATInputCode_KeyUp;
		case RETROK_DOWN: return kATInputCode_KeyDown;
		case RETROK_KP_ENTER: return kATInputCode_KeyNumpadEnter;
		case RETROK_KP_MULTIPLY: return kATInputCode_KeyMultiply;
		case RETROK_KP_PLUS: return kATInputCode_KeyAdd;
		case RETROK_KP_MINUS: return kATInputCode_KeySubtract;
		case RETROK_KP_PERIOD: return kATInputCode_KeyDecimal;
		case RETROK_KP_DIVIDE: return kATInputCode_KeyDivide;
		case RETROK_LSHIFT: return kATInputCode_KeyLShift;
		case RETROK_RSHIFT: return kATInputCode_KeyRShift;
		case RETROK_LCTRL: return kATInputCode_KeyLControl;
		case RETROK_RCTRL: return kATInputCode_KeyRControl;
		case RETROK_SEMICOLON: return kATInputCode_KeyOem1;
		case RETROK_EQUALS: return kATInputCode_KeyOemPlus;
		case RETROK_COMMA: return kATInputCode_KeyOemComma;
		case RETROK_MINUS: return kATInputCode_KeyOemMinus;
		case RETROK_PERIOD: return kATInputCode_KeyOemPeriod;
		case RETROK_SLASH: return kATInputCode_KeyOem2;
		case RETROK_BACKQUOTE: return kATInputCode_KeyOem3;
		case RETROK_LEFTBRACKET: return kATInputCode_KeyOem4;
		case RETROK_BACKSLASH: return kATInputCode_KeyOem5;
		case RETROK_RIGHTBRACKET: return kATInputCode_KeyOem6;
		default:
			return 0;
	}
}

bool IsRetroKeyExtended(unsigned keycode) {
	switch(keycode) {
		case RETROK_LEFT:
		case RETROK_RIGHT:
		case RETROK_UP:
		case RETROK_DOWN:
		case RETROK_INSERT:
		case RETROK_DELETE:
		case RETROK_HOME:
		case RETROK_END:
		case RETROK_PAGEUP:
		case RETROK_PAGEDOWN:
		case RETROK_KP_ENTER:
			return true;

		default:
			return false;
	}
}

constexpr unsigned kPolledKeyboardKeys[] = {
	RETROK_BACKSPACE,
	RETROK_TAB,
	RETROK_RETURN,
	RETROK_ESCAPE,
	RETROK_SPACE,
	RETROK_0,
	RETROK_1,
	RETROK_2,
	RETROK_3,
	RETROK_4,
	RETROK_5,
	RETROK_6,
	RETROK_7,
	RETROK_8,
	RETROK_9,
	RETROK_SEMICOLON,
	RETROK_EQUALS,
	RETROK_COMMA,
	RETROK_MINUS,
	RETROK_PERIOD,
	RETROK_SLASH,
	RETROK_LEFTBRACKET,
	RETROK_BACKSLASH,
	RETROK_RIGHTBRACKET,
	RETROK_BACKQUOTE,
	RETROK_a,
	RETROK_b,
	RETROK_c,
	RETROK_d,
	RETROK_e,
	RETROK_f,
	RETROK_g,
	RETROK_h,
	RETROK_i,
	RETROK_j,
	RETROK_k,
	RETROK_l,
	RETROK_m,
	RETROK_n,
	RETROK_o,
	RETROK_p,
	RETROK_q,
	RETROK_r,
	RETROK_s,
	RETROK_t,
	RETROK_u,
	RETROK_v,
	RETROK_w,
	RETROK_x,
	RETROK_y,
	RETROK_z,
	RETROK_DELETE,
	RETROK_KP0,
	RETROK_KP1,
	RETROK_KP2,
	RETROK_KP3,
	RETROK_KP4,
	RETROK_KP5,
	RETROK_KP6,
	RETROK_KP7,
	RETROK_KP8,
	RETROK_KP9,
	RETROK_KP_PERIOD,
	RETROK_KP_DIVIDE,
	RETROK_KP_MULTIPLY,
	RETROK_KP_MINUS,
	RETROK_KP_PLUS,
	RETROK_KP_ENTER,
	RETROK_UP,
	RETROK_DOWN,
	RETROK_RIGHT,
	RETROK_LEFT,
	RETROK_INSERT,
	RETROK_HOME,
	RETROK_END,
	RETROK_PAGEUP,
	RETROK_PAGEDOWN,
	RETROK_F1,
	RETROK_F2,
	RETROK_F3,
	RETROK_F4,
	RETROK_F5,
	RETROK_F6,
	RETROK_F7,
	RETROK_F8,
	RETROK_F9,
	RETROK_F10,
	RETROK_F11,
	RETROK_F12,
	RETROK_RSHIFT,
	RETROK_LSHIFT,
	RETROK_RCTRL,
	RETROK_LCTRL,
	RETROK_BREAK,
};

void SetKeyboardConsoleSwitch(uint32 scanCode, bool down) {
	size_t index = 0;
	uint8 bit = 0;

	switch(scanCode) {
		case kATUIKeyScanCode_Start:
			index = 0;
			bit = 0x01;
			break;

		case kATUIKeyScanCode_Select:
			index = 1;
			bit = 0x02;
			break;

		case kATUIKeyScanCode_Option:
			index = 2;
			bit = 0x04;
			break;

		default:
			return;
	}

	if (g_core.keyboardConsoleHeld[index] == down)
		return;

	g_core.keyboardConsoleHeld[index] = down;
	g_sim.GetGTIA().SetConsoleSwitch(bit, down);
}

void HandleKeyboardSpecialScanCode(uint32 scanCode, bool down) {
	if (scanCode == kATUIKeyScanCode_Break) {
		if (g_core.keyboardBreakHeld != down) {
			g_core.keyboardBreakHeld = down;
			g_sim.GetPokey().SetBreakKeyState(down, true);
		}
		return;
	}

	SetKeyboardConsoleSwitch(scanCode, down);
}

void PushKeyboardCharacter(uint32_t character) {
	if (!character)
		return;

	uint32 scanCode = 0;
	if (!ATUIGetScanCodeForCharacter32(character, scanCode)
		|| scanCode >= kATUIKeyScanCodeFirst)
	{
		return;
	}

	g_sim.GetPokey().PushKey((uint8)scanCode, false);
}

void HandleKeyboardEvent(bool down, unsigned keycode, uint32_t character) {
	if (!g_core.simulatorInitialized)
		return;

	ATInputManager *const im = g_sim.GetInputManager();
	const uint32 inputCode = MapRetroKeyToInputCode(keycode);
	if (!inputCode) {
		if (down)
			PushKeyboardCharacter(character);
		return;
	}

	auto it = std::find(g_core.keyboardHeldCodes.begin(),
		g_core.keyboardHeldCodes.end(), inputCode);
	const bool wasDown = it != g_core.keyboardHeldCodes.end();

	if (down == wasDown)
		return;

	const bool wasShift = g_core.keyboardHeldCodes.end() != std::find_if(
		g_core.keyboardHeldCodes.begin(), g_core.keyboardHeldCodes.end(),
		[](uint32 code) {
			return code == kATInputCode_KeyLShift
				|| code == kATInputCode_KeyRShift;
		});
	const bool wasCtrl = g_core.keyboardHeldCodes.end() != std::find_if(
		g_core.keyboardHeldCodes.begin(), g_core.keyboardHeldCodes.end(),
		[](uint32 code) {
			return code == kATInputCode_KeyLControl
				|| code == kATInputCode_KeyRControl;
		});

	if (down) {
		g_core.keyboardHeldCodes.push_back(inputCode);
		if (im)
			im->OnButtonDown(0, inputCode);
	} else {
		g_core.keyboardHeldCodes.erase(it);
		if (im)
			im->OnButtonUp(0, inputCode);
	}

	const bool shift = g_core.keyboardHeldCodes.end() != std::find_if(
		g_core.keyboardHeldCodes.begin(), g_core.keyboardHeldCodes.end(),
		[](uint32 code) {
			return code == kATInputCode_KeyLShift
				|| code == kATInputCode_KeyRShift;
		});
	const bool ctrl = g_core.keyboardHeldCodes.end() != std::find_if(
		g_core.keyboardHeldCodes.begin(), g_core.keyboardHeldCodes.end(),
		[](uint32 code) {
			return code == kATInputCode_KeyLControl
				|| code == kATInputCode_KeyRControl;
		});

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	if (shift != wasShift)
		pokey.SetShiftKeyState(shift, true);
	if (ctrl != wasCtrl)
		pokey.SetControlKeyState(ctrl);

	if (!down) {
		uint32 releaseScanCode = 0;
		if (ATUIGetScanCodeForVirtualKey(inputCode, false, wasCtrl, wasShift,
			IsRetroKeyExtended(keycode), releaseScanCode)
			&& releaseScanCode >= kATUIKeyScanCodeFirst
			&& releaseScanCode <= kATUIKeyScanCodeLast)
		{
			HandleKeyboardSpecialScanCode(releaseScanCode, false);
		}
		return;
	}

	if (character >= 0x80) {
		PushKeyboardCharacter(character);
		return;
	}

	uint32 scanCode = 0;
	if (!ATUIGetScanCodeForVirtualKey(inputCode, false, ctrl, shift,
		IsRetroKeyExtended(keycode), scanCode))
	{
		if (character)
			PushKeyboardCharacter(character);
		return;
	}

	if (scanCode >= kATUIKeyScanCodeFirst) {
		if (scanCode <= kATUIKeyScanCodeLast)
			HandleKeyboardSpecialScanCode(scanCode, true);
		return;
	}

	pokey.PushKey((uint8)scanCode, false);
}

void KeyboardCallback(bool down, unsigned keycode, uint32_t character, uint16_t) {
	g_core.keyboardCallbackEventSeen = true;
	HandleKeyboardEvent(down, keycode, character);
}

void PollKeyboardInput() {
	if (g_core.keyboardCallbackEventSeen)
		return;

	for(unsigned keycode : kPolledKeyboardKeys) {
		const bool down = g_inputState(
			0, RETRO_DEVICE_KEYBOARD, 0, keycode) != 0;
		HandleKeyboardEvent(down, keycode, 0);
	}
}

void SetMouseBeamFromNormalized(ATInputManager& im, float relX, float relY) {
	relX = std::clamp(relX, 0.0f, 1.0f);
	relY = std::clamp(relY, 0.0f, 1.0f);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	const float hcyc = (float)scanArea.left
		+ (relX * (float)scanArea.width()) - 0.5f;
	const float vcyc = (float)scanArea.top
		+ (relY * (float)scanArea.height()) - 0.5f;

	im.SetMouseBeamPos(
		(int)((hcyc - 128.0f) * (65536.0f / 94.0f)),
		(int)((vcyc - 128.0f) * (65536.0f / 188.0f)));
}

void SetMousePadFromNormalized(ATInputManager& im, float relX, float relY) {
	relX = std::clamp(relX, 0.0f, 1.0f);
	relY = std::clamp(relY, 0.0f, 1.0f);

	im.SetMousePadPos(
		(int)(relX * 131072.0f - 0x10000),
		(int)(relY * 131072.0f - 0x10000));
}

float LibretroScreenCoordToUnit(sint16 v) {
	return ((float)v + 32768.0f) * (1.0f / 65535.0f);
}

float LibretroPointerCoordToUnit(sint16 v) {
	return ((float)v + 32767.0f) * (1.0f / 65534.0f);
}

void PollMouseInput(ATInputManager& im) {
	for(unsigned port = 0; port < std::size(g_controllerDevices); ++port) {
		if (!IsSTMousePortEnabled(port))
			continue;

		const sint16 dx = g_inputState(
			port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
		const sint16 dy = g_inputState(
			port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
		if (dx || dy)
			im.OnMouseMove((int)port, dx, dy);
	}

	const unsigned mouseIds[] = {
		RETRO_DEVICE_ID_MOUSE_LEFT,
		RETRO_DEVICE_ID_MOUSE_RIGHT,
		RETRO_DEVICE_ID_MOUSE_MIDDLE,
		RETRO_DEVICE_ID_MOUSE_BUTTON_4,
		RETRO_DEVICE_ID_MOUSE_BUTTON_5,
	};

	const bool mouseButtonsActive = IsSTMousePortEnabled(0)
		|| IsSTMousePortEnabled(1)
		|| IsAbsolutePointerPortEnabled(0);

	for(size_t i = 0; i < std::size(mouseIds); ++i) {
		bool down = false;

		if (mouseButtonsActive) {
			if (IsSTMousePortEnabled(0))
				down = down || g_inputState(
					0, RETRO_DEVICE_MOUSE, 0, mouseIds[i]) != 0;
			if (IsSTMousePortEnabled(1))
				down = down || g_inputState(
					1, RETRO_DEVICE_MOUSE, 0, mouseIds[i]) != 0;
			if (IsAbsolutePointerPortEnabled(0)) {
				if (i == 0) {
					down = down || g_inputState(0, RETRO_DEVICE_LIGHTGUN, 0,
						RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) != 0;
					down = down || g_inputState(0, RETRO_DEVICE_POINTER, 0,
						RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
				}
			}
		}

		if (down == g_core.mouseButtonsHeld[i])
			continue;

		const uint32 code = GetMouseButtonInputCode(i);
		g_core.mouseButtonsHeld[i] = down;
		if (down)
			im.OnButtonDown(0, code);
		else
			im.OnButtonUp(0, code);
	}

	if (IsSTMousePortEnabled(0) || IsSTMousePortEnabled(1)) {
		const int wheelUp = g_inputState(
			0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP);
		const int wheelDown = g_inputState(
			0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN);
		if (wheelUp)
			im.OnMouseWheel(0, (float)wheelUp);
		if (wheelDown)
			im.OnMouseWheel(0, (float)-wheelDown);
	}
}

void PollAbsolutePointerInput(ATInputManager& im) {
	if (!IsAbsolutePointerPortEnabled(0))
		return;

	bool havePosition = false;
	float relX = 0.5f;
	float relY = 0.5f;

	if (!g_inputState(0, RETRO_DEVICE_LIGHTGUN, 0,
		RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN))
	{
		const sint16 x = g_inputState(0, RETRO_DEVICE_LIGHTGUN, 0,
			RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
		const sint16 y = g_inputState(0, RETRO_DEVICE_LIGHTGUN, 0,
			RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
		if (x || y) {
			relX = LibretroScreenCoordToUnit(x);
			relY = LibretroScreenCoordToUnit(y);
			havePosition = true;
		}
	}

	if (!havePosition && !g_inputState(0, RETRO_DEVICE_POINTER, 0,
		RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN))
	{
		const sint16 x = g_inputState(0, RETRO_DEVICE_POINTER, 0,
			RETRO_DEVICE_ID_POINTER_X);
		const sint16 y = g_inputState(0, RETRO_DEVICE_POINTER, 0,
			RETRO_DEVICE_ID_POINTER_Y);

		if (x || y || g_inputState(0, RETRO_DEVICE_POINTER, 0,
			RETRO_DEVICE_ID_POINTER_PRESSED))
		{
			relX = LibretroPointerCoordToUnit(x);
			relY = LibretroPointerCoordToUnit(y);
			havePosition = true;
		}
	}

	if (!havePosition)
		return;

	SetMouseBeamFromNormalized(im, relX, relY);
	SetMousePadFromNormalized(im, relX, relY);
}

void InitDefaultInputMaps() {
	ATInputManager *im = g_sim.GetInputManager();
	if (!im)
		return;

	im->ResetToDefaults();

	const uint32 mapCount = im->GetInputMapCount();
	for(uint32 i = 0; i < mapCount; ++i) {
		ATInputMap *map = nullptr;
		if (!im->GetInputMapByIndex(i, &map) || !map)
			continue;

		const bool activate =
			(IsJoystickPortEnabled(0)
				&& map->HasController(kATInputControllerType_Joystick, 0))
			|| (Is5200PortEnabled(0)
				&& map->HasController(kATInputControllerType_5200Controller, 0))
			|| (IsPaddlePortEnabled(0)
				&& map->HasController(kATInputControllerType_Paddle,
					GetPaddleIndexForPort(0)))
			|| (IsSTMousePortEnabled(0)
				&& map->HasController(kATInputControllerType_STMouse, 0))
			|| (IsLightPenPortEnabled(0)
				&& map->HasController(kATInputControllerType_LightPen, 0))
			|| (IsLightGunPortEnabled(0)
				&& map->HasController(kATInputControllerType_LightGun, 0))
			|| (IsJoystickPortEnabled(1)
				&& map->HasController(kATInputControllerType_Joystick, 1))
			|| (IsSTMousePortEnabled(1)
				&& map->HasController(kATInputControllerType_STMouse, 1));
		im->ActivateInputMap(map, activate);
		map->Release();
	}

	if (IsPaddlePortEnabled(0))
		AddLibretroPaddleMap(*im, 0);
	if (IsPaddlePortEnabled(1))
		AddLibretroPaddleMap(*im, 1);
}

void ReleaseInput() {
	ATInputManager *im = g_sim.GetInputManager();

	if (im) {
		for(size_t unit = 0; unit < std::size(g_core.buttonsHeld); ++unit) {
			for(size_t i = 0; i < std::size(kRetropadButtonMap); ++i) {
				if (g_core.buttonsHeld[unit][i]) {
					im->OnButtonUp((int)unit, g_core.buttonHeldCodes[unit][i]);
					g_core.buttonsHeld[unit][i] = false;
					g_core.buttonHeldCodes[unit][i] = 0;
				}
			}
		}

		for(uint32 inputCode : g_core.keyboardHeldCodes)
			im->OnButtonUp(0, inputCode);
		g_core.keyboardHeldCodes.clear();

		for(size_t i = 0; i < std::size(g_core.mouseButtonsHeld); ++i) {
			if (g_core.mouseButtonsHeld[i]) {
				im->OnButtonUp(0, GetMouseButtonInputCode(i));
				g_core.mouseButtonsHeld[i] = false;
			}
		}
	}

	for(size_t i = 0; i < std::size(kConsoleSwitchBits); ++i) {
		if (g_core.consoleHeld[i]) {
			g_sim.GetGTIA().SetConsoleSwitch(kConsoleSwitchBits[i], false);
			g_core.consoleHeld[i] = false;
		}

		if (g_core.keyboardConsoleHeld[i]) {
			g_sim.GetGTIA().SetConsoleSwitch(kConsoleSwitchBits[i], false);
			g_core.keyboardConsoleHeld[i] = false;
		}
	}

	if (g_core.keyboardBreakHeld) {
		g_sim.GetPokey().SetBreakKeyState(false, true);
		g_core.keyboardBreakHeld = false;
	}

	g_sim.GetPokey().SetShiftKeyState(false, true);
	g_sim.GetPokey().SetControlKeyState(false);
	g_sim.GetPokey().ReleaseAllRawKeys(true);
}

void InvalidateSerializeCache() {
	g_core.serializeCache.clear();
	g_core.serializeCacheValid = false;
}

void WriteLE32(uint8_t *dst, uint32 v) {
	dst[0] = (uint8_t)v;
	dst[1] = (uint8_t)(v >> 8);
	dst[2] = (uint8_t)(v >> 16);
	dst[3] = (uint8_t)(v >> 24);
}

uint32 ReadLE32(const uint8_t *src) {
	return (uint32)src[0]
		| ((uint32)src[1] << 8)
		| ((uint32)src[2] << 16)
		| ((uint32)src[3] << 24);
}

void UpdateInput() {
	if (!g_inputState)
		return;

	ATInputManager *im = g_sim.GetInputManager();

	if (im) {
		for(unsigned port = 0; port < std::size(g_core.buttonsHeld); ++port) {
			const bool portJoypad = (g_controllerDevices[port] & RETRO_DEVICE_MASK)
				== RETRO_DEVICE_JOYPAD;
			const bool portPaddle = IsPaddlePortEnabled(port);
			const uint16 joypadState = GetRetropadStateMask(port);

			for(size_t i = 0; i < std::size(kRetropadButtonMap); ++i) {
				const uint32 inputCode = (portJoypad || portPaddle)
					? GetRetropadInputCode(port, kRetropadButtonMap[i])
					: 0;
				const bool active = inputCode != 0;
				bool down = false;

				if (active) {
					down = (joypadState
						& (uint16)(1U << kRetropadButtonMap[i].retroId)) != 0;

					if (!down && portPaddle
						&& (kRetropadButtonMap[i].retroId == RETRO_DEVICE_ID_JOYPAD_B
							|| kRetropadButtonMap[i].retroId == RETRO_DEVICE_ID_JOYPAD_A))
					{
						down = g_inputState(
							port, RETRO_DEVICE_ANALOG,
							RETRO_DEVICE_INDEX_ANALOG_BUTTON,
							kRetropadButtonMap[i].retroId) > 0;
					}
				}

				if (g_core.buttonsHeld[port][i]
					&& g_core.buttonHeldCodes[port][i] != inputCode)
				{
					im->OnButtonUp((int)port, g_core.buttonHeldCodes[port][i]);
					g_core.buttonsHeld[port][i] = false;
					g_core.buttonHeldCodes[port][i] = 0;
				}

				if (down == g_core.buttonsHeld[port][i])
					continue;

				g_core.buttonsHeld[port][i] = down;
				g_core.buttonHeldCodes[port][i] = down ? inputCode : 0;

				if (down)
					im->OnButtonDown((int)port, inputCode);
				else
					im->OnButtonUp((int)port, inputCode);
			}

			if (portPaddle) {
				const sint16 x = g_inputState(
					port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
					RETRO_DEVICE_ID_ANALOG_X);
				im->OnAxisInput((int)port, kATInputCode_JoyHoriz1, x, x);
			}
		}

		PollMouseInput(*im);
		PollAbsolutePointerInput(*im);
	}

	const bool port0Joypad = (g_controllerDevices[0] & RETRO_DEVICE_MASK)
		== RETRO_DEVICE_JOYPAD;
	const bool port0Active = port0Joypad && IsJoystickPortEnabled(0);
	const uint16 port0JoypadState = port0Active ? GetRetropadStateMask(0) : 0;

	for(size_t i = 0; i < std::size(kConsoleRetroIds); ++i) {
		const bool down = port0Active
			&& (port0JoypadState & (uint16)(1U << kConsoleRetroIds[i])) != 0;

		if (down == g_core.consoleHeld[i])
			continue;

		g_core.consoleHeld[i] = down;
		g_sim.GetGTIA().SetConsoleSwitch(kConsoleSwitchBits[i], down);
	}

	PollKeyboardInput();
}

size_t SubmitAudio(const sint16 *data, uint32 frames) {
	if (!frames)
		return 0;

	if (g_audioBatch)
		return g_audioBatch(data, frames);

	if (g_audioSample) {
		for(uint32 i = 0; i < frames; ++i)
			g_audioSample(data[i * 2], data[i * 2 + 1]);

		return frames;
	}

	return 0;
}

void SetStaticEnvironment() {
	if (!g_env)
		return;

	QueryCoreDirectories();
	RegisterCoreOptions();
	RegisterDiskControl();
	QueryInputBitmaskSupport();

	bool supportsNoGame = true;
	g_env(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &supportsNoGame);

	bool supportsAchievements = false;
	g_env(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &supportsAchievements);

	static const retro_input_descriptor inputDescriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Joystick Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Joystick Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Joystick Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Trigger" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Trigger" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "START" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "SELECT" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "OPTION" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Joystick 2 Up" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Joystick 2 Down" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Joystick 2 Left" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick 2 Right" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Joystick 2 Trigger" },
		{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Joystick 2 Trigger" },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Paddle Knob" },
		{ 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_B, "Paddle Trigger" },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Paddle 2 Knob" },
		{ 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_B, "Paddle 2 Trigger" },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X, "Mouse X" },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y, "Mouse Y" },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT, "Mouse Left Button" },
		{ 0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT, "Mouse Right Button" },
		{ 1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X, "Mouse 2 X" },
		{ 1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y, "Mouse 2 Y" },
		{ 1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT, "Mouse 2 Left Button" },
		{ 1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT, "Mouse 2 Right Button" },
		{ 0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X, "Light Gun X" },
		{ 0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y, "Light Gun Y" },
		{ 0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER, "Light Gun Trigger" },
		{ 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X, "Pointer X" },
		{ 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y, "Pointer Y" },
		{ 0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED, "Pointer Pressed" },
		{ 0, 0, 0, 0, nullptr },
	};

	g_env(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)inputDescriptors);

	static const retro_keyboard_callback keyboardCallback {
		KeyboardCallback
	};
	g_env(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, (void *)&keyboardCallback);

	static const retro_controller_description portControllers[] = {
		{ "Atari Joystick", RETRO_DEVICE_JOYPAD },
		{ "Atari Paddle", RETRO_DEVICE_ANALOG },
		{ "Atari ST Mouse", RETRO_DEVICE_MOUSE },
		{ "Atari Light Gun/Pen", RETRO_DEVICE_LIGHTGUN },
		{ "None", RETRO_DEVICE_NONE },
	};
	static const retro_controller_info controllerInfo[] = {
		{ portControllers, (unsigned)std::size(portControllers) },
		{ portControllers, (unsigned)std::size(portControllers) },
		{ nullptr, 0 },
	};

	g_env(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)controllerInfo);
}

retro_game_geometry MakeGeometry(unsigned w, unsigned h) {
	retro_game_geometry geometry {};
	geometry.base_width = w;
	geometry.base_height = h;
	geometry.max_width = 912;
	geometry.max_height = 624;
	geometry.aspect_ratio = 4.0f / 3.0f;
	return geometry;
}

void GetCurrentFrameGeometry(unsigned& w, unsigned& h) {
	if (g_core.lastFrameW > 0 && g_core.lastFrameH > 0) {
		w = (unsigned)g_core.lastFrameW;
		h = (unsigned)g_core.lastFrameH;
		return;
	}

	if (g_core.simulatorInitialized) {
		int rw = 0;
		int rh = 0;
		bool rgb32 = false;
		g_sim.GetGTIA().GetRawFrameFormat(rw, rh, rgb32);

		if (rw > 0 && rh > 0) {
			w = (unsigned)rw;
			h = (unsigned)rh;
			return;
		}
	}

	w = 336;
	h = 224;
}

void FillAvInfo(retro_system_av_info& info) {
	std::memset(&info, 0, sizeof(info));

	unsigned w = 0;
	unsigned h = 0;
	GetCurrentFrameGeometry(w, h);
	info.geometry = MakeGeometry(w, h);

	const ATVideoStandard standard = g_core.simulatorInitialized
		? g_sim.GetVideoStandard()
		: g_core.pendingVideoStandard;
	info.timing.fps =
		(standard != kATVideoStandard_NTSC && standard != kATVideoStandard_PAL60)
			? (double)kATFrameRate_PAL
			: (double)kATFrameRate_NTSC;
	info.timing.sample_rate = 48000.0;
}

void ReportGeometry(unsigned w, unsigned h, bool force) {
	if (!g_env || !w || !h)
		return;

	if (!force
		&& g_core.reportedGeometryW == w
		&& g_core.reportedGeometryH == h)
	{
		return;
	}

	retro_game_geometry geometry = MakeGeometry(w, h);
	g_env(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
	g_core.reportedGeometryW = w;
	g_core.reportedGeometryH = h;
}

bool InitSimulator() {
	if (g_core.simulatorInitialized)
		return true;

	VDRegistryAppKey::setDefaultKey("AltirraSDL");
	ATRegistryLoadFromDisk();
	ATInitSaveStateDeserializer();
	ATVFSInstallAtfsHandler();

	g_sim.Init();
	RegisterRetroArchFirmwareDirectories();
	g_sim.SetRandomSeed((uint32)std::rand() ^ ((uint32)std::rand() << 15));
	g_sim.LoadROMs();

	g_core.nullDisplay = ATLibretroCreateNullVideoDisplay();
	g_sim.GetGTIA().SetVideoOutput(g_core.nullDisplay);
	g_sim.GetGTIA().SetFrameSkip(true);

	ATRegisterDevices(*g_sim.GetDeviceManager());
	ATRegisterDeviceXCmds(*g_sim.GetDeviceManager());
	ATSocketInit();
	ATLoadConfigVars();
	ATOptionsLoad();
	ATLoadDefaultProfiles();
	ATSettingsLoadLastProfile((ATSettingsCategory)(
		kATSettingsCategory_All
		& ~kATSettingsCategory_FullScreen
		& ~kATSettingsCategory_Input
		& ~kATSettingsCategory_InputMaps
	));
	ATUIInitVirtualKeyMap(g_kbdOpts);
	InitDefaultInputMaps();
	ReadResetOptions();
	ApplyPendingResetOptions(true);
	ApplyLiveOptions();

	ATInitDebugger();

	g_sim.ColdReset();
	g_sim.Resume();

	g_core.simulatorInitialized = true;
	g_core.lastStandard = g_sim.GetVideoStandard();
	return true;
}

void ApplyUpdatedCoreOptions() {
	if (!g_core.simulatorInitialized || !g_env)
		return;

	bool optionsUpdated = false;
	if (!g_env(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &optionsUpdated)
		|| !optionsUpdated)
	{
		return;
	}

	const ATVideoStandard oldVideoStandard = g_sim.GetVideoStandard();
	ReadCoreOptions();

	if (!ApplyPendingResetOptions(false))
		return;

	ReleaseInput();
	InvalidateSerializeCache();
	g_sim.ColdReset();
	g_sim.Resume();

	if (oldVideoStandard != g_sim.GetVideoStandard()) {
		retro_system_av_info av {};
		FillAvInfo(av);
		g_env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
	}
}

void ShutdownSimulator() {
	if (!g_core.simulatorInitialized)
		return;

	g_sim.Pause();
	ReleaseInput();
	g_sim.GetGTIA().SetVideoOutput(nullptr);

	if (g_core.nullDisplay) {
		g_core.nullDisplay->Destroy();
		g_core.nullDisplay = nullptr;
	}

	ATShutdownDebugger();
	g_sim.Shutdown();
	g_core = CoreState {};
}

void SubmitCurrentFrame() {
	if (!g_video)
		return;

	if (ATLibretroCaptureXrgb(g_sim, g_core.frameBuffer)) {
		const int w = g_core.frameBuffer.w;
		const int h = g_core.frameBuffer.h;
		const ptrdiff_t pitch = g_core.frameBuffer.pitch;
		const size_t frameBytes = (size_t)pitch * (size_t)h;

		g_core.lastFrame.assign(
			(const uint8_t *)g_core.frameBuffer.data,
			(const uint8_t *)g_core.frameBuffer.data + frameBytes);
		g_core.lastFrameW = w;
		g_core.lastFrameH = h;
		g_core.lastFramePitch = pitch;

		ReportGeometry((unsigned)w, (unsigned)h, false);
		g_video(g_core.frameBuffer.data, (unsigned)w, (unsigned)h, (size_t)pitch);
		return;
	}

	if (!g_core.lastFrame.empty()) {
		g_video(g_core.lastFrame.data(), (unsigned)g_core.lastFrameW,
			(unsigned)g_core.lastFrameH, (size_t)g_core.lastFramePitch);
	} else {
		g_video(nullptr, 0, 0, 0);
	}
}

bool BuildSerializeCache() {
	if (!g_core.simulatorInitialized || !g_core.gameLoaded)
		return false;

	try {
		vdrefptr<IATSerializable> snapshot;
		vdrefptr<IATSerializable> snapshotInfo;
		g_sim.CreateSnapshot(~snapshot, ~snapshotInfo);

		VDMemoryBufferStream stream;
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(stream));

		{
			vdautoptr<IATSaveStateSerializer> ser(
				ATCreateSaveStateSerializer(L"savestate.json"));
			ser->Serialize(*zip, *snapshot);
		}

		{
			vdautoptr<IATSaveStateSerializer> ser(
				ATCreateSaveStateSerializer(L"savestateinfo.json"));
			ser->Serialize(*zip, *snapshotInfo);
		}

		zip->Finalize();

		const auto payload = stream.GetBuffer();
		const size_t fixedSize = g_core.serializeFixedSize;

		if (!fixedSize || payload.size() > fixedSize - kStateHeaderSize)
			return false;

		g_core.serializeCache.assign(fixedSize, 0);
		std::memcpy(g_core.serializeCache.data(), kStateMagic, sizeof kStateMagic);
		WriteLE32(g_core.serializeCache.data() + 8, kStateVersion);
		WriteLE32(g_core.serializeCache.data() + 12, (uint32)payload.size());
		WriteLE32(g_core.serializeCache.data() + 16,
			VDCRCTable::CRC32.CRC(payload.data(), payload.size()));
		std::memcpy(g_core.serializeCache.data() + kStateHeaderSize,
			payload.data(), payload.size());
		g_core.serializeCacheValid = true;
		return true;
	} catch(...) {
		InvalidateSerializeCache();
		return false;
	}
}

bool LoadSerializedState(const void *data, size_t size) {
	if (!g_core.simulatorInitialized || !g_core.gameLoaded || !data || !size)
		return false;

	if (size < kStateHeaderSize || size > 0x7FFFFFFF)
		return false;

	const uint8_t *const src = (const uint8_t *)data;
	if (std::memcmp(src, kStateMagic, sizeof kStateMagic))
		return false;

	if (ReadLE32(src + 8) != kStateVersion)
		return false;

	const uint32 payloadSize = ReadLE32(src + 12);
	const uint32 payloadCrc = ReadLE32(src + 16);

	if (payloadSize > size - kStateHeaderSize)
		return false;

	const uint8_t *const payload = src + kStateHeaderSize;
	if (VDCRCTable::CRC32.CRC(payload, payloadSize) != payloadCrc)
		return false;

	try {
		VDMemoryStream stream(payload, payloadSize);
		VDZipArchive zip;
		zip.Init(&stream);

		vdrefptr<IATSerializable> snapshot;
		vdautoptr<IATSaveStateDeserializer> ds(
			ATCreateSaveStateDeserializer(L"savestate.json"));
		ds->Deserialize(zip, ~snapshot);

		if (!snapshot)
			return false;

		ReleaseInput();
		const bool ok = g_sim.ApplySnapshot(*snapshot, nullptr);
		g_sim.Resume();
		InvalidateSerializeCache();
		return ok;
	} catch(...) {
		return false;
	}
}
}

extern "C" {

RETRO_API unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

RETRO_API void retro_set_environment(retro_environment_t cb) {
	g_env = cb;
	SetStaticEnvironment();
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) {
	g_video = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) {
	g_audioSample = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
	g_audioBatch = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb) {
	g_inputPoll = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb) {
	g_inputState = cb;
}

RETRO_API void retro_init(void) {
	ATLibretroSetAudioSink(SubmitAudio);
}

RETRO_API void retro_deinit(void) {
	ShutdownSimulator();
	ATLibretroSetAudioSink(nullptr);
}

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
	if (!info)
		return;

	std::memset(info, 0, sizeof(*info));
	info->library_name = "Altirra";
	info->library_version = AT_VERSION;
	info->valid_extensions = kValidExtensions;
	info->need_fullpath = true;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
	if (!info)
		return;

	std::memset(info, 0, sizeof(*info));
	FillAvInfo(*info);
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
	if (port < std::size(g_controllerDevices)) {
		g_controllerDevices[port] = device;

		if (g_core.simulatorInitialized) {
			ReleaseInput();
			InitDefaultInputMaps();
		}
	}
}

RETRO_API void retro_reset(void) {
	if (g_core.simulatorInitialized) {
		InvalidateSerializeCache();
		ReadResetOptions();
		ApplyPendingResetOptions(true);
		ApplyLiveOptions();
		g_sim.ColdReset();
		g_sim.Resume();
	}
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
	retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (g_env && !g_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;

	if (game && !game->path)
		return false;

	if (!InitSimulator())
		return false;

	if (game && game->path && *game->path) {
		if (HasExtension(game->path, "m3u")) {
			if (!LoadM3U(game->path))
				return false;

			if (!MountDiskIndex(g_core.diskIndex))
				return false;
		} else {
			ATImageLoadContext ctx {};
			const VDStringW wpath = VDTextU8ToW(VDStringSpanA(game->path));

			if (!g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx))
				return false;

			if (IsDiskPath(game->path)) {
				g_core.diskImages.clear();
				g_core.diskImages.push_back({ game->path, GetPathLabel(game->path) });
				g_core.diskIndex = 0;
				g_core.diskEjected = false;
			}
		}
	}

	g_sim.ColdReset();
	g_sim.Resume();
	InvalidateSerializeCache();
	g_core.gameLoaded = true;
	g_core.serializeFixedSize = kStateFixedMaxSize;

	if (g_env) {
		retro_system_av_info av {};
		FillAvInfo(av);
		g_env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
	}

	return true;
}

RETRO_API void retro_unload_game(void) {
	if (g_core.simulatorInitialized) {
		ReleaseInput();
		g_sim.Pause();
		g_sim.UnloadAll();
	}

	g_core.gameLoaded = false;
	g_core.serializeFixedSize = 0;
	InvalidateSerializeCache();
	g_core.lastFrame.clear();
	g_core.lastFrameW = 0;
	g_core.lastFrameH = 0;
	g_core.lastFramePitch = 0;
	g_core.diskImages.clear();
	g_core.diskIndex = 0;
	g_core.diskEjected = false;
}

RETRO_API void retro_run(void) {
	if (g_inputPoll)
		g_inputPoll();

	ApplyUpdatedCoreOptions();

	bool ranFrame = false;
	if (g_core.gameLoaded) {
		UpdateInput();

		for (;;) {
			const ATSimulator::AdvanceResult r = g_sim.Advance(false);

			if (g_core.nullDisplay
				&& ATLibretroNullVideoDisplayConsumeFramePosted(g_core.nullDisplay))
				break;

			if (r == ATSimulator::kAdvanceResult_Stopped)
				break;
		}

		SubmitCurrentFrame();
		InvalidateSerializeCache();
		ranFrame = true;
	} else
	if (g_video)
		g_video(nullptr, 0, 0, 0);

	if (!ranFrame && g_audioBatch)
		g_audioBatch(nullptr, 0);
	else if (!ranFrame && g_audioSample)
		g_audioSample(0, 0);
}

RETRO_API size_t retro_serialize_size(void) {
	if (!g_core.simulatorInitialized || !g_core.gameLoaded)
		return 0;

	return g_core.serializeFixedSize;
}

RETRO_API bool retro_serialize(void *data, size_t size) {
	if (!g_core.serializeCacheValid && !BuildSerializeCache())
		return false;

	if (!data || size < g_core.serializeCache.size())
		return false;

	std::memcpy(data, g_core.serializeCache.data(), g_core.serializeCache.size());
	return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size) {
	return LoadSerializedState(data, size);
}

RETRO_API void retro_cheat_reset(void) {
}

RETRO_API void retro_cheat_set(unsigned, bool, const char *) {
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t) {
	return false;
}

RETRO_API unsigned retro_get_region(void) {
	if (g_core.simulatorInitialized && g_sim.IsVideo50Hz())
		return RETRO_REGION_PAL;
	return RETRO_REGION_NTSC;
}

RETRO_API void *retro_get_memory_data(unsigned) {
	return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned) {
	return 0;
}

}
