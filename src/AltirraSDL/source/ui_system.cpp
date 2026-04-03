//	AltirraSDL - System Configuration dialog
//	Mirrors Windows Altirra's Configure System paged dialog
//	(IDD_CONFIGURE with tree sidebar IDC_PAGE_TREE).
//	Hierarchy: Computer, Outputs, Peripherals, Media, Emulator.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>
#include <vd2/system/unknown.h>

#include "ui_main.h"
#include "simulator.h"
#include "constants.h"
#include "cpu.h"
#include "firmwaremanager.h"
#include "devicemanager.h"
#include "diskinterface.h"
#include "cartridge.h"
#include "gtia.h"
#include "cassette.h"
#include "options.h"
#include "uiaccessors.h"
#include <at/atcore/media.h>
#include <at/atcore/device.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/propertyset.h>
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>
#include <at/atio/cassetteimage.h>
#include "inputcontroller.h"
#include "compatengine.h"
#include "firmwaredetect.h"
#include <vd2/system/file.h>
#include <mutex>
#include <thread>
#include <unordered_map>

extern ATSimulator g_sim;
void ATUIUpdateSpeedTiming();
void ATUIResizeDisplay();
void ATSyncCPUHistoryState();

// Firmware Manager window visibility (used by drag-and-drop handler in main loop)

// Forward declaration: defined in ui_firmware.cpp
void RenderFirmwareCategory(ATSimulator &sim);

// =========================================================================
// Category IDs (flat index used by systemConfigCategory in ATUIState)
// Matches Windows tree order from uiconfiguresystem.cpp OnPopulatePages()
// =========================================================================

enum {
	// Top-level (before categories)
	kCat_Overview,      // Overview (system config summary)
	kCat_Recommendations, // Recommendations (assessment)
	// Computer
	kCat_System,        // Computer > System
	kCat_CPU,           // Computer > CPU
	kCat_Firmware,      // Computer > Firmware
	kCat_Memory,        // Computer > Memory
	kCat_Acceleration,  // Computer > Acceleration
	kCat_Speed,         // Computer > Speed
	kCat_Boot,          // Computer > Boot
	// Outputs
	kCat_Video,         // Outputs > Video
	kCat_EnhancedText,  // Outputs > Enhanced Text
	kCat_Audio,         // Outputs > Audio
	// Peripherals
	kCat_Devices,       // Peripherals > Devices
	kCat_Keyboard,      // Peripherals > Keyboard
	// Media
	kCat_MediaDefaults, // Media > Defaults
	kCat_Disk,          // Media > Disk
	kCat_Cassette,      // Media > Cassette
	kCat_Flash,         // Media > Flash
	// Emulator
	kCat_CompatDB,      // Emulator > Compat DB
	kCat_Display,       // Emulator > Display
	kCat_EaseOfUse,     // Emulator > Ease of Use
	kCat_ErrorHandling, // Emulator > Error Handling
	kCat_Input,         // Emulator > Input
	kCat_Caption,       // Emulator > Window Caption
	kCat_Workarounds,   // Emulator > Workarounds
	kCat_Count
};

// =========================================================================
// Overview (matches Windows IDD_CONFIGURE_OVERVIEW — system config summary)
// =========================================================================

static void RenderOverviewCategory(ATSimulator &sim) {
	ImGui::SeparatorText("System Configuration Overview");

	// Base system
	const char *vsName = "?";
	switch (sim.GetVideoStandard()) {
		case kATVideoStandard_NTSC:   vsName = "NTSC"; break;
		case kATVideoStandard_NTSC50: vsName = "NTSC50"; break;
		case kATVideoStandard_PAL:    vsName = "PAL"; break;
		case kATVideoStandard_PAL60:  vsName = "PAL60"; break;
		case kATVideoStandard_SECAM:  vsName = "SECAM"; break;
	}

	const char *modelName = "?";
	switch (sim.GetHardwareMode()) {
		case kATHardwareMode_800:    modelName = "800"; break;
		case kATHardwareMode_800XL:  modelName = "800XL"; break;
		case kATHardwareMode_5200:   modelName = "5200 SuperSystem"; break;
		case kATHardwareMode_XEGS:   modelName = "XE Game System (XEGS)"; break;
		case kATHardwareMode_1200XL: modelName = "1200XL"; break;
		case kATHardwareMode_130XE:  modelName = "130XE"; break;
		case kATHardwareMode_1400XL: modelName = "1400XL"; break;
	}

	const char *memStr = "?";
	switch (sim.GetMemoryMode()) {
		case kATMemoryMode_8K:          memStr = "8K"; break;
		case kATMemoryMode_16K:         memStr = "16K"; break;
		case kATMemoryMode_24K:         memStr = "24K"; break;
		case kATMemoryMode_32K:         memStr = "32K"; break;
		case kATMemoryMode_40K:         memStr = "40K"; break;
		case kATMemoryMode_48K:         memStr = "48K"; break;
		case kATMemoryMode_52K:         memStr = "52K"; break;
		case kATMemoryMode_64K:         memStr = "64K"; break;
		case kATMemoryMode_128K:        memStr = "128K"; break;
		case kATMemoryMode_256K:        memStr = "256K"; break;
		case kATMemoryMode_320K:        memStr = "320K"; break;
		case kATMemoryMode_320K_Compy:  memStr = "320K Compy"; break;
		case kATMemoryMode_576K:        memStr = "576K"; break;
		case kATMemoryMode_576K_Compy:  memStr = "576K Compy"; break;
		case kATMemoryMode_1088K:       memStr = "1088K"; break;
	}

	if (ImGui::BeginTable("##OverviewTable", 2, ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextUnformatted("Base system");
		ImGui::TableNextColumn(); ImGui::Text("%s %s (%s)", vsName, modelName, memStr);

		// OS firmware
		ATFirmwareManager *fwm = sim.GetFirmwareManager();
		if (fwm) {
			ATFirmwareInfo fwInfo;
			if (fwm->GetFirmwareInfo(sim.GetActualKernelId(), fwInfo)) {
				VDStringA fwName = VDTextWToU8(fwInfo.mName);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("OS firmware");
				ImGui::TableNextColumn(); ImGui::Text("%s [%08X]", fwName.c_str(), sim.ComputeKernelCRC32());
			} else {
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("OS firmware");
				ImGui::TableNextColumn(); ImGui::Text("Internal [%08X]", sim.ComputeKernelCRC32());
			}
		}

		// Additional devices
		ATDeviceManager *devMgr = sim.GetDeviceManager();
		if (devMgr) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted("Additional devices");
			ImGui::TableNextColumn();

			VDStringA devList;
			for (IATDevice *dev : devMgr->GetDevices(true, true, true)) {
				ATDeviceInfo info;
				dev->GetDeviceInfo(info);
				if (!devList.empty())
					devList += ", ";
				devList += VDTextWToU8(VDStringW(info.mpDef->mpName));
			}
			if (devList.empty())
				devList = "None";
			ImGui::TextWrapped("%s", devList.c_str());
		}

		// Mounted images
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextUnformatted("Mounted images");
		ImGui::TableNextColumn();

		bool foundImage = false;
		for (int i = 0; i < 15; ++i) {
			ATDiskInterface& di = sim.GetDiskInterface(i);
			IATDiskImage *image = di.GetDiskImage();
			if (image) {
				VDStringA name = VDTextWToU8(VDStringW(VDFileSplitPath(di.GetPath())));
				auto crc = image->GetImageFileCRC();
				if (crc.has_value())
					ImGui::Text("Disk: %s [%08X]", name.c_str(), crc.value());
				else
					ImGui::Text("Disk: %s", name.c_str());
				foundImage = true;
			}
		}

		for (uint32 i = 0; i < 2; ++i) {
			ATCartridgeEmulator *ce = sim.GetCartridge(i);
			if (ce) {
				const wchar_t *path = ce->GetPath();
				if (path && *path) {
					VDStringA name = VDTextWToU8(VDStringW(VDFileSplitPath(path)));
					auto crc = ce->GetImageFileCRC();
					if (crc.has_value())
						ImGui::Text("Cartridge: %s [%08X]", name.c_str(), crc.value());
					else
						ImGui::Text("Cartridge: %s", name.c_str());
					foundImage = true;
				}
			}
		}

		if (!foundImage)
			ImGui::TextUnformatted("None");

		// Debugging options
		bool haveDebug = false;
		if (sim.GetMemoryClearMode() != kATMemoryClearMode_DRAM1) {
			if (!haveDebug) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Debugging");
				ImGui::TableNextColumn();
				haveDebug = true;
			}
			ImGui::TextUnformatted("Memory randomization changed");
		}

		if (sim.IsRandomFillEXEEnabled()) {
			if (!haveDebug) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Debugging");
				ImGui::TableNextColumn();
				haveDebug = true;
			}
			ImGui::TextUnformatted("Randomize memory on EXE start");
		}

		// Firmware issues
		if (devMgr) {
			bool firmwareIssue = false;
			for (IATDeviceFirmware *fw : devMgr->GetInterfaces<IATDeviceFirmware>(false, true, false)) {
				if (fw->GetFirmwareStatus() != ATDeviceFirmwareStatus::OK) {
					firmwareIssue = true;
					break;
				}
			}

			if (firmwareIssue) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted("Issues");
				ImGui::TableNextColumn();
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
					"A device has missing or invalid firmware. Check Devices page.");
			}
		}

		ImGui::EndTable();
	}

	ImGui::Separator();

	if (ImGui::Button("Copy to Clipboard")) {
		VDStringA text;
		text.sprintf("Base system: %s %s (%s)\n", vsName, modelName, memStr);

		ATFirmwareManager *fwm2 = sim.GetFirmwareManager();
		if (fwm2) {
			ATFirmwareInfo fwInfo;
			if (fwm2->GetFirmwareInfo(sim.GetActualKernelId(), fwInfo)) {
				VDStringA fwName = VDTextWToU8(fwInfo.mName);
				text.append_sprintf("OS firmware: %s [%08X]\n", fwName.c_str(), sim.ComputeKernelCRC32());
			}
		}

		ImGui::SetClipboardText(text.c_str());
	}
}

// =========================================================================
// Recommendations (matches Windows IDD_CONFIGURE_ASSESSMENT)
// =========================================================================

static void RenderRecommendationsCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Assess for:");

	static int sAssessMode = 0;
	static const char *kAssessModes[] = { "Compatibility", "Accuracy", "Emulator Performance" };
	ImGui::Combo("##AssessTarget", &sAssessMode, kAssessModes, 3);

	ImGui::Separator();

	bool anyRecommendation = false;

	auto addBullet = [&](const char *text) {
		ImGui::Bullet();
		ImGui::TextWrapped("%s", text);
		anyRecommendation = true;
	};

	// Assessment for Compatibility (mode 0)
	if (sAssessMode == 0) {
		// Firmware check
		if (sim.GetActualKernelId() < kATFirmwareId_Custom)
			addBullet("AltirraOS is being used as the current operating system. This will work with most well-behaved software, but some programs only work with the Atari OS.");

		// CPU mode
		switch (sim.GetCPU().GetCPUMode()) {
			case kATCPUMode_65C02:
				addBullet("CPU mode is set to 65C02. Most software expects a 6502.");
				break;
			case kATCPUMode_65C816:
				addBullet("CPU mode is set to 65C816. Most software expects a 6502.");
				break;
			default:
				break;
		}

		// Memory too low
		switch (sim.GetMemoryMode()) {
			case kATMemoryMode_8K:
			case kATMemoryMode_16K:
			case kATMemoryMode_24K:
			case kATMemoryMode_32K:
			case kATMemoryMode_40K:
				addBullet("System memory is below 48K. Most programs need at least 48K to run correctly.");
				break;
			default:
				break;
		}

		// Fast FP
		if (sim.IsFPPatchEnabled())
			addBullet("Fast floating-point math acceleration is enabled. BASIC programs will execute much faster than normal.");

		// BASIC enabled
		if (sim.IsBASICEnabled())
			addBullet("Internal BASIC is enabled. Non-BASIC programs often require BASIC to be disabled by holding Option on boot.");

		// PAL
		if (sim.GetVideoStandard() == kATVideoStandard_PAL)
			addBullet("Video standard is set to PAL (50Hz). Games written for NTSC regions will execute slower than intended.");

		// Keyboard mode
		{
			extern ATUIKeyboardOptions g_kbdOpts;
			if (!g_kbdOpts.mbRawKeys)
				addBullet("The keyboard mode is set to Cooked. This makes it easier to type text but can cause issues with programs that check for held keys.");
		}

		// Stop on BRK
		if (sim.GetCPU().GetStopOnBRK())
			addBullet("The Stop on BRK Instruction debugging option is enabled. Occasionally some programs require BRK instructions to run properly.");
	}

	// Assessment for Accuracy (mode 1)
	if (sAssessMode == 1) {
		if (sim.GetActualKernelId() < kATFirmwareId_Custom)
			addBullet("AltirraOS is being used. Some programs only work correctly with the original Atari OS ROM.");

		if (sim.IsSIOPatchEnabled())
			addBullet("Disk accesses are being accelerated by SIO patch.");

		if (sim.GetDiskBurstTransfersEnabled())
			addBullet("Disk burst I/O is enabled.");

		if (sim.IsFPPatchEnabled())
			addBullet("Fast floating-point math acceleration is enabled. BASIC programs will execute much faster than normal.");

		if (sim.GetVideoStandard() == kATVideoStandard_PAL)
			addBullet("Video standard is set to PAL (50Hz). Games written for NTSC regions will execute slower than intended.");

		if (sim.GetVideoStandard() == kATVideoStandard_NTSC)
			addBullet("Video standard is set to NTSC (60Hz). Games written for PAL regions will execute faster than intended and may malfunction.");

		{
			extern ATUIKeyboardOptions g_kbdOpts;
			if (!g_kbdOpts.mbRawKeys)
				addBullet("The keyboard mode is set to Cooked. This makes it easier to type but can cause issues with programs that check for held keys.");
		}
	}

	// Assessment for Performance (mode 2)
	if (sAssessMode == 2) {
		if (sim.GetCPU().IsHistoryEnabled())
			addBullet("CPU execution history is enabled. If not needed, turning it off will slightly improve performance.");
	}

	if (!anyRecommendation)
		ImGui::TextDisabled("No recommendations.");
}

// =========================================================================
// System (Hardware)
// =========================================================================

static const ATHardwareMode kHWModeValues[] = {
	kATHardwareMode_800, kATHardwareMode_800XL, kATHardwareMode_1200XL,
	kATHardwareMode_130XE, kATHardwareMode_1400XL, kATHardwareMode_XEGS,
	kATHardwareMode_5200,
};
static const char *kHWModeLabels[] = {
	"Atari 800", "Atari 800XL", "Atari 1200XL",
	"Atari 130XE", "Atari 1400XL", "Atari XEGS",
	"Atari 5200",
};
static const int kHWModeCount = 7;

static const ATVideoStandard kVideoStdValues[] = {
	kATVideoStandard_NTSC, kATVideoStandard_PAL, kATVideoStandard_SECAM,
	kATVideoStandard_NTSC50, kATVideoStandard_PAL60,
};
static const char *kVideoStdLabels[] = { "NTSC", "PAL", "SECAM", "NTSC50", "PAL60" };
static const int kVideoStdCount = 5;

static void RenderSystemCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Hardware type");

	ATHardwareMode curHW = sim.GetHardwareMode();
	int hwIdx = 0;
	for (int i = 0; i < kHWModeCount; ++i)
		if (kHWModeValues[i] == curHW) { hwIdx = i; break; }
	if (ImGui::Combo("##HardwareType", &hwIdx, kHWModeLabels, kHWModeCount))
		ATUISwitchHardwareMode(nullptr, kHWModeValues[hwIdx], false);
	ImGui::SetItemTooltip("Select the Atari hardware model to emulate.");

	ImGui::SeparatorText("Video standard");

	// 5200 mode is always NTSC — disable video standard controls
	const bool is5200 = (sim.GetHardwareMode() == kATHardwareMode_5200);

	ATVideoStandard curVS = sim.GetVideoStandard();
	int vsIdx = 0;
	for (int i = 0; i < kVideoStdCount; ++i)
		if (kVideoStdValues[i] == curVS) { vsIdx = i; break; }

	if (is5200) ImGui::BeginDisabled();
	if (ImGui::Combo("##VideoStandard", &vsIdx, kVideoStdLabels, kVideoStdCount)) {
		sim.SetVideoStandard(kVideoStdValues[vsIdx]);
		ATUIUpdateSpeedTiming();
		// Conditionally cold reset based on Ease of Use setting
		// (matches Windows ATUIConfirmVideoStandardChangeResetComplete)
		if (ATUIIsResetNeeded(kATUIResetFlag_VideoStandardChange))
			sim.ColdReset();
	}

	ImGui::SameLine();
	if (ImGui::Button("Toggle NTSC/PAL")) {
		sim.SetVideoStandard(curVS == kATVideoStandard_NTSC ? kATVideoStandard_PAL : kATVideoStandard_NTSC);
		ATUIUpdateSpeedTiming();
		if (ATUIIsResetNeeded(kATUIResetFlag_VideoStandardChange))
			sim.ColdReset();
	}
	if (is5200) ImGui::EndDisabled();

	ImGui::SeparatorText("CTIA/GTIA type");

	bool ctia = sim.GetGTIA().IsCTIAMode();
	if (ImGui::Checkbox("CTIA mode", &ctia))
		sim.GetGTIA().SetCTIAMode(ctia);
	ImGui::SetItemTooltip("Enable CTIA mode instead of GTIA. The CTIA is an earlier graphics chip that lacks some GTIA display modes.");

	ImGui::SeparatorText("GTIA defect emulation");

	static const char *kDefectLabels[] = { "None", "Type 1 (earlier chip)", "Type 2 (later chip)" };
	ATGTIADefectMode curDefect = sim.GetGTIA().GetDefectMode();
	int defIdx = (int)curDefect;
	if (defIdx < 0 || defIdx > 2) defIdx = 1;
	if (ImGui::Combo("##DefectMode", &defIdx, kDefectLabels, 3))
		sim.GetGTIA().SetDefectMode((ATGTIADefectMode)defIdx);
	ImGui::SetItemTooltip("Emulate specific GTIA chip revisions that have different color behavior in certain graphics modes.");
}

// =========================================================================
// CPU (matches Windows IDD_CONFIGURE_CPU — radio buttons for all modes)
// =========================================================================

struct CPUModeEntry {
	ATCPUMode mode;
	uint32 subCycles;
	const char *label;
};

static const CPUModeEntry kCPUModes[] = {
	{ kATCPUMode_6502,  1, "6502 / 6502C" },
	{ kATCPUMode_65C02, 1, "65C02" },
	{ kATCPUMode_65C816, 1, "65C816 (1.79MHz)" },
	{ kATCPUMode_65C816, 2, "65C816 (3.58MHz)" },
	{ kATCPUMode_65C816, 4, "65C816 (7.14MHz)" },
	{ kATCPUMode_65C816, 6, "65C816 (10.74MHz)" },
	{ kATCPUMode_65C816, 8, "65C816 (14.28MHz)" },
	{ kATCPUMode_65C816, 10, "65C816 (17.90MHz)" },
	{ kATCPUMode_65C816, 12, "65C816 (21.48MHz)" },
	{ kATCPUMode_65C816, 23, "65C816 (41.16MHz)" },
};
static const int kNumCPUModes = 10;

static void RenderCPUCategory(ATSimulator &sim) {
	ImGui::SeparatorText("CPU mode");

	ATCPUMode curMode = sim.GetCPU().GetCPUMode();
	uint32 curSub = sim.GetCPU().GetSubCycles();

	int cpuIdx = 0;
	for (int i = 0; i < kNumCPUModes; ++i)
		if (kCPUModes[i].mode == curMode && kCPUModes[i].subCycles == curSub) { cpuIdx = i; break; }

	for (int i = 0; i < kNumCPUModes; ++i) {
		if (ImGui::RadioButton(kCPUModes[i].label, cpuIdx == i)) {
			// Only cold reset if CPU mode actually changes (not just speed)
			// Matches Windows OnCommandSystemCPUMode: reset only when
			// !IsCPUModeOverridden() and mode != current mode
			bool needReset = (!sim.IsCPUModeOverridden() && kCPUModes[i].mode != curMode);
			sim.SetCPUMode(kCPUModes[i].mode, kCPUModes[i].subCycles);
			if (needReset)
				sim.ColdReset();
		}
	}

	ImGui::SeparatorText("Additional options");

	bool illegals = sim.GetCPU().AreIllegalInsnsEnabled();
	if (ImGui::Checkbox("Enable illegal instructions", &illegals))
		sim.GetCPU().SetIllegalInsnsEnabled(illegals);
	ImGui::SetItemTooltip("Allow execution of undocumented 6502 instructions. Required by some programs.");

	bool nmiBlock = sim.GetCPU().IsNMIBlockingEnabled();
	if (ImGui::Checkbox("Allow BRK/IRQ to block NMI", &nmiBlock))
		sim.GetCPU().SetNMIBlockingEnabled(nmiBlock);
	ImGui::SetItemTooltip("Emulate the 6502 hardware bug where a BRK or IRQ can block an NMI.");

	bool stopBRK = sim.GetCPU().GetStopOnBRK();
	if (ImGui::Checkbox("Stop on BRK instruction", &stopBRK))
		sim.GetCPU().SetStopOnBRK(stopBRK);
	ImGui::SetItemTooltip("Pause emulation when a BRK instruction is executed. Useful for debugging.");

	bool history = sim.GetCPU().IsHistoryEnabled();
	if (ImGui::Checkbox("Record instruction history", &history)) {
		sim.GetCPU().SetHistoryEnabled(history);
		ATSyncCPUHistoryState();
	}
	ImGui::SetItemTooltip("Record CPU instruction execution history for the debugger. Uses more memory when enabled.");

	bool paths = sim.GetCPU().IsPathfindingEnabled();
	if (ImGui::Checkbox("Track code paths", &paths))
		sim.GetCPU().SetPathfindingEnabled(paths);
	ImGui::SetItemTooltip("Track code execution paths for the debugger profiler.");

	bool shadowROM = sim.GetShadowROMEnabled();
	if (ImGui::Checkbox("Shadow ROMs in fast RAM", &shadowROM))
		sim.SetShadowROMEnabled(shadowROM);
	ImGui::SetItemTooltip("Copy ROM into fast RAM banks for accelerated access with 65C816. Requires 65C816 CPU mode.");

	bool shadowCart = sim.GetShadowCartridgeEnabled();
	if (ImGui::Checkbox("Shadow cartridges in fast RAM", &shadowCart))
		sim.SetShadowCartridgeEnabled(shadowCart);
	ImGui::SetItemTooltip("Copy cartridge ROM into fast RAM banks for accelerated access with 65C816.");
}


// =========================================================================
// Memory (matches Windows IDD_CONFIGURE_MEMORY)
// =========================================================================

static const ATMemoryMode kMemModeValues[] = {
	kATMemoryMode_8K, kATMemoryMode_16K, kATMemoryMode_24K,
	kATMemoryMode_32K, kATMemoryMode_40K, kATMemoryMode_48K,
	kATMemoryMode_52K, kATMemoryMode_64K, kATMemoryMode_128K,
	kATMemoryMode_256K, kATMemoryMode_320K, kATMemoryMode_576K,
	kATMemoryMode_1088K,
};
static const char *kMemModeLabels[] = {
	"8K", "16K", "24K", "32K", "40K", "48K", "52K", "64K",
	"128K", "256K", "320K (Compy Shop)", "576K (Compy Shop)", "1088K",
};

static const ATMemoryClearMode kMemClearValues[] = {
	kATMemoryClearMode_Zero, kATMemoryClearMode_Random,
	kATMemoryClearMode_DRAM1, kATMemoryClearMode_DRAM2, kATMemoryClearMode_DRAM3,
};
static const char *kMemClearLabels[] = {
	"Zero", "Random", "DRAM Pattern 1", "DRAM Pattern 2", "DRAM Pattern 3",
};

static void RenderMemoryCategory(ATSimulator &sim) {
	ATMemoryMode curMM = sim.GetMemoryMode();
	int mmIdx = 0;
	for (int i = 0; i < 13; ++i)
		if (kMemModeValues[i] == curMM) { mmIdx = i; break; }
	if (ImGui::Combo("Memory Size", &mmIdx, kMemModeLabels, 13))
		ATUISwitchMemoryMode(nullptr, kMemModeValues[mmIdx]);
	ImGui::SetItemTooltip("Set the amount of main and extended memory installed in the emulated computer.");

	ATMemoryClearMode curMC = sim.GetMemoryClearMode();
	int mcIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kMemClearValues[i] == curMC) { mcIdx = i; break; }
	if (ImGui::Combo("Memory Clear Pattern", &mcIdx, kMemClearLabels, 5))
		sim.SetMemoryClearMode(kMemClearValues[mcIdx]);
	ImGui::SetItemTooltip("Set the memory pattern stored in RAM on power-up.");

	ImGui::Separator();

	bool mapRAM = sim.IsMapRAMEnabled();
	if (ImGui::Checkbox("Enable MapRAM (XL/XE only)", &mapRAM))
		sim.SetMapRAMEnabled(mapRAM);
	ImGui::SetItemTooltip("Allow normally inaccessible 2K of memory under the OS ROM to be used as RAM.");

	bool u1mb = sim.IsUltimate1MBEnabled();
	if (ImGui::Checkbox("Enable Ultimate1MB", &u1mb)) {
		sim.SetUltimate1MBEnabled(u1mb);
		sim.ColdReset();
	}
	ImGui::SetItemTooltip("Emulate the Ultimate1MB memory expansion.");

	bool axlonAlias = sim.GetAxlonAliasingEnabled();
	if (ImGui::Checkbox("Enable bank register aliasing", &axlonAlias))
		sim.SetAxlonAliasingEnabled(axlonAlias);
	ImGui::SetItemTooltip("Enable emulation of the bank register alias at $0FFF used by some Axlon-compatible RAM disks.");

	bool floatingIO = sim.IsFloatingIoBusEnabled();
	if (ImGui::Checkbox("Enable floating I/O bus (800 only)", &floatingIO)) {
		sim.SetFloatingIoBusEnabled(floatingIO);
		sim.ColdReset();
	}
	ImGui::SetItemTooltip("Emulate 800-specific behavior where the I/O bus floats when no device responds.");

	ImGui::SeparatorText("Axlon banked memory (800 only)");

	// Matches Windows System.AxlonMemory (None/64K/128K/.../4096K)
	static const char *kAxlonLabels[] = {
		"None", "64K (2 bits)", "128K (3 bits)", "256K (4 bits)",
		"512K (5 bits)", "1024K (6 bits)", "2048K (7 bits)", "4096K (8 bits)"
	};
	static const uint8 kAxlonBits[] = { 0, 2, 3, 4, 5, 6, 7, 8 };
	const bool is5200 = (sim.GetHardwareMode() == kATHardwareMode_5200);
	uint8 curAxlon = sim.GetAxlonMemoryMode();
	int axlonIdx = 0;
	for (int i = 0; i < 8; ++i)
		if (kAxlonBits[i] == curAxlon) { axlonIdx = i; break; }
	if (is5200) ImGui::BeginDisabled();
	if (ImGui::Combo("Axlon banks", &axlonIdx, kAxlonLabels, 8)) {
		sim.SetAxlonMemoryMode(kAxlonBits[axlonIdx]);
		sim.ColdReset();
	}
	ImGui::SetItemTooltip("Set the size of extended memory accessed through the Axlon RAM disk protocol at $0FFF.");
	if (is5200) ImGui::EndDisabled();

	ImGui::SeparatorText("65C816 high memory");

	// Matches Windows System.HighMemBanks (Auto/-1, or explicit bank counts)
	static const char *kHighMemLabels[] = {
		"Auto", "None (0)", "4 MB", "8 MB", "16 MB"
	};
	static const sint32 kHighMemValues[] = { -1, 0, 64, 128, 256 };
	bool is65C816 = (sim.GetCPU().GetCPUMode() == kATCPUMode_65C816);
	sint32 curHighMem = sim.GetHighMemoryBanks();
	int hmIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kHighMemValues[i] == curHighMem) { hmIdx = i; break; }
	if (!is65C816) ImGui::BeginDisabled();
	if (ImGui::Combo("High memory banks", &hmIdx, kHighMemLabels, 5)) {
		sim.SetHighMemoryBanks(kHighMemValues[hmIdx]);
		sim.ColdReset();
	}
	ImGui::SetItemTooltip("Set the amount of memory available above bank 0 for the 65C816 CPU.");
	if (!is65C816) ImGui::EndDisabled();

	ImGui::Separator();

	bool preserveExt = sim.IsPreserveExtRAMEnabled();
	if (ImGui::Checkbox("Preserve extended memory on cold reset", &preserveExt))
		sim.SetPreserveExtRAMEnabled(preserveExt);
	ImGui::SetItemTooltip("Keep contents of extended memory intact during cold reset.");
}

// =========================================================================
// Acceleration (matches Windows IDD_CONFIGURE_ACCELERATION)
// =========================================================================

static void RenderAccelerationCategory(ATSimulator &sim) {
	ImGui::SeparatorText("OS acceleration");

	bool fastBoot = sim.IsFastBootEnabled();
	if (ImGui::Checkbox("Fast boot", &fastBoot))
		sim.SetFastBootEnabled(fastBoot);
	ImGui::SetItemTooltip("Accelerate standard OS checksum and memory test routines on cold boot.");

	bool fastFP = sim.IsFPPatchEnabled();
	if (ImGui::Checkbox("Fast floating-point math", &fastFP))
		sim.SetFPPatchEnabled(fastFP);
	ImGui::SetItemTooltip("Intercept calls to the floating-point math pack and execute them at native speed.");

	ImGui::SeparatorText("SIO device patches");

	bool sioPatch = sim.IsSIOPatchEnabled();
	if (ImGui::Checkbox("SIO Patch", &sioPatch))
		sim.SetSIOPatchEnabled(sioPatch);

	bool diskSioPatch = sim.IsDiskSIOPatchEnabled();
	if (ImGui::Checkbox("D: patch (Disk SIO)", &diskSioPatch))
		sim.SetDiskSIOPatchEnabled(diskSioPatch);
	ImGui::SetItemTooltip("Intercept and accelerate serial I/O transfers to disk drive devices.");

	bool casSioPatch = sim.IsCassetteSIOPatchEnabled();
	if (ImGui::Checkbox("C: patch (Cassette SIO)", &casSioPatch))
		sim.SetCassetteSIOPatchEnabled(casSioPatch);
	ImGui::SetItemTooltip("Intercept and accelerate serial I/O transfers to the cassette.");

	bool deviceSioPatch = sim.GetDeviceSIOPatchEnabled();
	if (ImGui::Checkbox("PRT: patch (Other SIO)", &deviceSioPatch))
		sim.SetDeviceSIOPatchEnabled(deviceSioPatch);
	ImGui::SetItemTooltip("Intercept and accelerate serial I/O transfers to other SIO bus devices.");

	ImGui::SeparatorText("CIO device patches");

	bool cioH = sim.GetCIOPatchEnabled('H');
	if (ImGui::Checkbox("H: (Host device CIO)", &cioH))
		sim.SetCIOPatchEnabled('H', cioH);
	ImGui::SetItemTooltip("Intercept and accelerate CIO transfers to the host device.");

	bool cioP = sim.GetCIOPatchEnabled('P');
	if (ImGui::Checkbox("P: (Printer CIO)", &cioP))
		sim.SetCIOPatchEnabled('P', cioP);
	ImGui::SetItemTooltip("Intercept and accelerate CIO transfers to the printer device.");

	bool cioR = sim.GetCIOPatchEnabled('R');
	if (ImGui::Checkbox("R: (RS-232 CIO)", &cioR))
		sim.SetCIOPatchEnabled('R', cioR);
	ImGui::SetItemTooltip("Intercept and accelerate CIO transfers to the RS-232 serial device.");

	bool cioT = sim.GetCIOPatchEnabled('T');
	if (ImGui::Checkbox("T: (1030 Serial CIO)", &cioT))
		sim.SetCIOPatchEnabled('T', cioT);
	ImGui::SetItemTooltip("Intercept and accelerate CIO transfers to the 1030 serial device.");

	ImGui::SeparatorText("Burst transfers");

	bool diskBurst = sim.GetDiskBurstTransfersEnabled();
	if (ImGui::Checkbox("D: burst I/O", &diskBurst))
		sim.SetDiskBurstTransfersEnabled(diskBurst);
	ImGui::SetItemTooltip("Speed up transfers to disk drives by bursting data without per-byte delays.");

	bool devSioBurst = sim.GetDeviceSIOBurstTransfersEnabled();
	if (ImGui::Checkbox("PRT: burst I/O", &devSioBurst))
		sim.SetDeviceSIOBurstTransfersEnabled(devSioBurst);
	ImGui::SetItemTooltip("Speed up transfers to other devices by bursting data without per-byte delays.");

	bool devCioBurst = sim.GetDeviceCIOBurstTransfersEnabled();
	if (ImGui::Checkbox("CIO burst transfers", &devCioBurst))
		sim.SetDeviceCIOBurstTransfersEnabled(devCioBurst);
	ImGui::SetItemTooltip("Detect and accelerate large block CIO transfers.");

	ImGui::SeparatorText("SIO accelerator mode");

	// Matches Windows Devices.SIOAccelMode (Patch/PBI/Both)
	bool sioPatchOn = sim.IsSIOPatchEnabled();
	bool sioPBI = sim.IsSIOPBIPatchEnabled();
	int sioAccelMode = 0;
	if (sioPatchOn && !sioPBI) sioAccelMode = 0;
	else if (!sioPatchOn && sioPBI) sioAccelMode = 1;
	else if (sioPatchOn && sioPBI) sioAccelMode = 2;
	if (ImGui::RadioButton("Software patch##SIO", sioAccelMode == 0)) {
		sim.SetSIOPatchEnabled(true); sim.SetSIOPBIPatchEnabled(false);
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("PBI patch##SIO", sioAccelMode == 1)) {
		sim.SetSIOPatchEnabled(false); sim.SetSIOPBIPatchEnabled(true);
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Both##SIO", sioAccelMode == 2)) {
		sim.SetSIOPatchEnabled(true); sim.SetSIOPBIPatchEnabled(true);
	}

	ImGui::SeparatorText("CIO hook mode");

	// Matches Windows Devices.CIOHookMode (Hardware/PBI)
	bool cioPBI = sim.IsCIOPBIPatchEnabled();
	if (ImGui::RadioButton("Hardware##CIO", !cioPBI))
		sim.SetCIOPBIPatchEnabled(false);
	ImGui::SameLine();
	if (ImGui::RadioButton("PBI##CIO", cioPBI))
		sim.SetCIOPBIPatchEnabled(true);

	ImGui::Separator();

	bool sioOverride = sim.IsDiskSIOOverrideDetectEnabled();
	if (ImGui::Checkbox("SIO override detection", &sioOverride))
		sim.SetDiskSIOOverrideDetectEnabled(sioOverride);
	ImGui::SetItemTooltip("Attempt to detect when the OS or firmware is intercepting SIO requests and disable acceleration.");
}

// =========================================================================
// Speed (matches Windows IDD_CONFIGURE_SPEED)
// =========================================================================

static void RenderSpeedCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Speed control");

	bool warp = ATUIGetTurbo();
	if (ImGui::Checkbox("Run as fast as possible (warp)", &warp))
		ATUISetTurbo(warp);
	ImGui::SetItemTooltip("Disable the speed limiter and run the emulation as fast as possible.");

	bool slowmo = ATUIGetSlowMotion();
	if (ImGui::Checkbox("Slow Motion", &slowmo))
		ATUISetSlowMotion(slowmo);

	ImGui::SeparatorText("Speed adjustment");

	float spd = ATUIGetSpeedModifier();
	static const float kSpdValues[] = { 0.0f, 1.0f, 3.0f, 7.0f };
	static const char *kSpdLabels[] = { "1x (Normal)", "2x", "4x", "8x" };
	int spdIdx = 0;
	for (int i = 0; i < 4; ++i)
		if (kSpdValues[i] == spd) { spdIdx = i; break; }
	if (ImGui::Combo("Speed", &spdIdx, kSpdLabels, 4))
		ATUISetSpeedModifier(kSpdValues[spdIdx]);
	ImGui::SetItemTooltip("Scale the baseline rate to run the emulation faster or slower.");

	ImGui::SeparatorText("Frame rate");

	static const char *kFrameRateLabels[] = { "Hardware", "Broadcast", "Integral" };
	ATFrameRateMode frMode = ATUIGetFrameRateMode();
	int frIdx = (int)frMode;
	if (frIdx < 0 || frIdx >= 3) frIdx = 0;
	if (ImGui::Combo("Base frame rate", &frIdx, kFrameRateLabels, 3))
		ATUISetFrameRateMode((ATFrameRateMode)frIdx);
	ImGui::SetItemTooltip("Select the baseline rate at which the emulator runs.");

	bool vsyncAdaptive = ATUIGetFrameRateVSyncAdaptive();
	if (ImGui::Checkbox("Lock speed to display refresh rate", &vsyncAdaptive))
		ATUISetFrameRateVSyncAdaptive(vsyncAdaptive);
	ImGui::SetItemTooltip("Lock emulation speed to the display's refresh rate.");

	ImGui::Separator();

	bool pauseInactive = ATUIGetPauseWhenInactive();
	if (ImGui::Checkbox("Pause when emulator window is inactive", &pauseInactive))
		ATUISetPauseWhenInactive(pauseInactive);
	ImGui::SetItemTooltip("Automatically pause the emulation when the emulator window is inactive.");
}

// =========================================================================
// Boot (matches Windows IDD_CONFIGURE_BOOT)
// =========================================================================

static void RenderBootCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Program load mode");

	static const char *kLoadModes[] = {
		"Default", "Type 3 Poll", "Deferred", "Disk Boot"
	};
	int loadMode = (int)sim.GetHLEProgramLoadMode();
	if (loadMode < 0 || loadMode > 3) loadMode = 0;
	if (ImGui::Combo("##ProgramLoadMode", &loadMode, kLoadModes, 4))
		sim.SetHLEProgramLoadMode((ATHLEProgramLoadMode)loadMode);
	ImGui::SetItemTooltip("Method to use when booting binary programs.");

	ImGui::SeparatorText("Randomization");

	bool randomFillEXE = sim.IsRandomFillEXEEnabled();
	if (ImGui::Checkbox("Randomize Memory on EXE Load", &randomFillEXE))
		sim.SetRandomFillEXEEnabled(randomFillEXE);

	bool randomLaunch = sim.IsRandomProgramLaunchDelayEnabled();
	if (ImGui::Checkbox("Randomize program load timing", &randomLaunch))
		sim.SetRandomProgramLaunchDelayEnabled(randomLaunch);
	ImGui::SetItemTooltip("Start program after a random delay to exercise timing-sensitive code.");

	ImGui::SeparatorText("Unload on boot image");

	uint32 bootMask = ATUIGetBootUnloadStorageMask();

	bool unloadCarts = (bootMask & kATStorageTypeMask_Cartridge) != 0;
	if (ImGui::Checkbox("Unload cartridges when booting new image", &unloadCarts))
		ATUISetBootUnloadStorageMask(unloadCarts
			? (bootMask | kATStorageTypeMask_Cartridge)
			: (bootMask & ~kATStorageTypeMask_Cartridge));
	ImGui::SetItemTooltip("Selects image types to automatically unload when booting a new image.");

	bool unloadDisks = (bootMask & kATStorageTypeMask_Disk) != 0;
	if (ImGui::Checkbox("Unload disks when booting new image", &unloadDisks))
		ATUISetBootUnloadStorageMask(unloadDisks
			? (bootMask | kATStorageTypeMask_Disk)
			: (bootMask & ~kATStorageTypeMask_Disk));
	ImGui::SetItemTooltip("Selects image types to automatically unload when booting a new image.");

	bool unloadTapes = (bootMask & kATStorageTypeMask_Tape) != 0;
	if (ImGui::Checkbox("Unload tapes when booting new image", &unloadTapes))
		ATUISetBootUnloadStorageMask(unloadTapes
			? (bootMask | kATStorageTypeMask_Tape)
			: (bootMask & ~kATStorageTypeMask_Tape));
	ImGui::SetItemTooltip("Selects image types to automatically unload when booting a new image.");

	ImGui::SeparatorText("Power-on delay");

	// Matches Windows System.PowerOnDelay (Auto/None/1s/2s/3s)
	static const char *kPowerOnLabels[] = { "Auto", "None", "1 sec", "2 sec", "3 sec" };
	static const int kPowerOnValues[] = { -1, 0, 10, 20, 30 };
	int curDelay = sim.GetPowerOnDelay();
	int delayIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kPowerOnValues[i] == curDelay) { delayIdx = i; break; }
	if (ImGui::Combo("Power-on delay", &delayIdx, kPowerOnLabels, 5))
		sim.SetPowerOnDelay(kPowerOnValues[delayIdx]);

	ImGui::SeparatorText("Random seed");

	uint32 currentSeed = sim.GetRandomSeed();
	ImGui::Text("Current seed: %u", currentSeed);

	uint32 lockedSeed = sim.GetLockedRandomSeed();
	if (lockedSeed)
		ImGui::Text("Next seed: %u (locked)", lockedSeed);
	else
		ImGui::Text("Next seed: Auto");

	static char seedBuf[32] = {};
	ImGui::SetNextItemWidth(150);
	ImGui::InputText("##SeedInput", seedBuf, sizeof(seedBuf), ImGuiInputTextFlags_CharsDecimal);
	ImGui::SameLine();
	if (ImGui::Button("Use Specific Seed")) {
		unsigned long val = strtoul(seedBuf, nullptr, 10);
		sim.SetLockedRandomSeed((uint32)val);
	}
	ImGui::SameLine();
	if (ImGui::Button("Auto")) {
		sim.SetLockedRandomSeed(0);
		seedBuf[0] = 0;
	}
	ImGui::SetItemTooltip("Clear locked seed — use automatic randomization.");
}

// =========================================================================
// Video (matches Windows IDD_CONFIGURE_VIDEO)
// =========================================================================

static void RenderVideoCategory(ATSimulator &sim) {
	ATGTIAEmulator& gtia = sim.GetGTIA();

	ImGui::SeparatorText("Video effects");

	static const char *kArtifactLabels[] = {
		"None", "NTSC", "PAL", "NTSC High", "PAL High", "Auto", "Auto High"
	};
	int artifact = (int)gtia.GetArtifactingMode();
	if (artifact < 0 || artifact >= 7) artifact = 0;
	if (ImGui::Combo("Artifacting", &artifact, kArtifactLabels, 7))
		gtia.SetArtifactingMode((ATArtifactMode)artifact);
	ImGui::SetItemTooltip("Emulate false color effects derived from composite video encoding.");

	static const char *kMonitorLabels[] = {
		"Color", "Peritel", "Green Mono", "Amber Mono", "Blue-White Mono", "White Mono"
	};
	int monitor = (int)gtia.GetMonitorMode();
	if (monitor < 0 || monitor >= 6) monitor = 0;
	if (ImGui::Combo("Monitor Mode", &monitor, kMonitorLabels, 6))
		gtia.SetMonitorMode((ATMonitorMode)monitor);
	ImGui::SetItemTooltip("Selects the monitor (screen) type.");

	ImGui::Separator();

	bool blend = gtia.IsBlendModeEnabled();
	if (ImGui::Checkbox("Frame Blending", &blend))
		gtia.SetBlendModeEnabled(blend);
	ImGui::SetItemTooltip("Blend adjacent frames together to eliminate flickering from alternating frame techniques.");

	bool linearBlend = gtia.IsLinearBlendEnabled();
	if (ImGui::Checkbox("Linear Frame Blending", &linearBlend))
		gtia.SetLinearBlendEnabled(linearBlend);
	ImGui::SetItemTooltip("Use linear color blending for more accurate colors when frame blending.");

	bool monoPersist = gtia.IsBlendMonoPersistenceEnabled();
	if (ImGui::Checkbox("Mono Persistence", &monoPersist))
		gtia.SetBlendMonoPersistenceEnabled(monoPersist);
	ImGui::SetItemTooltip("Emulate phosphor persistence on monochrome monitors.");

	bool interlace = gtia.IsInterlaceEnabled();
	if (ImGui::Checkbox("Interlace", &interlace)) {
		gtia.SetInterlaceEnabled(interlace);
		ATUIResizeDisplay();
	}
	ImGui::SetItemTooltip("Enable support for displaying video as interlaced fields.");

	bool scanlines = gtia.AreScanlinesEnabled();
	if (ImGui::Checkbox("Scanlines", &scanlines)) {
		gtia.SetScanlinesEnabled(scanlines);
		ATUIResizeDisplay();
	}
	ImGui::SetItemTooltip("Darken video between scanlines to simulate CRT beam scanning.");

	ImGui::Separator();

	// PAL phase (matches Windows Video.PALPhase0/1 from cmds.cpp)
	int palPhase = gtia.GetPALPhase();
	static const char *kPALPhaseLabels[] = { "Phase 0 (standard)", "Phase 1 (alternate)" };
	if (palPhase < 0 || palPhase > 1) palPhase = 0;
	if (ImGui::Combo("PAL Phase", &palPhase, kPALPhaseLabels, 2))
		gtia.SetPALPhase(palPhase);
	ImGui::SetItemTooltip("Controls the V-phase of even and odd lines for PAL video output.");

	bool palExt = gtia.IsOverscanPALExtended();
	if (ImGui::Checkbox("Extended PAL Height", &palExt))
		gtia.SetOverscanPALExtended(palExt);
	ImGui::SetItemTooltip("Show additional scanlines visible in PAL mode.");
}

// =========================================================================
// Audio (matches Windows IDD_CONFIGURE_AUDIO)
// =========================================================================

static void RenderAudioCategory(ATSimulator &sim, ATUIState &state) {
	ImGui::SeparatorText("Audio setup");

	IATAudioOutput *pAudio = sim.GetAudioOutput();
	if (pAudio) {
		bool muted = pAudio->GetMute();
		if (ImGui::Checkbox("Mute All", &muted))
			pAudio->SetMute(muted);
		ImGui::SetItemTooltip("Mute all audio output.");
	}

	bool dualPokey = sim.IsDualPokeysEnabled();
	if (ImGui::Checkbox("Stereo", &dualPokey))
		sim.SetDualPokeysEnabled(dualPokey);
	ImGui::SetItemTooltip("Enable emulation of two POKEYs, controlling the left and right channels.");

	ATPokeyEmulator& pokey = sim.GetPokey();

	bool stereoMono = pokey.IsStereoAsMonoEnabled();
	if (ImGui::Checkbox("Downmix stereo to mono", &stereoMono))
		pokey.SetStereoAsMonoEnabled(stereoMono);
	ImGui::SetItemTooltip("Downmix stereo audio from dual POKEYs to a single mono output.");

	bool nonlinear = pokey.IsNonlinearMixingEnabled();
	if (ImGui::Checkbox("Non-linear mixing", &nonlinear))
		pokey.SetNonlinearMixingEnabled(nonlinear);
	ImGui::SetItemTooltip("Emulate analog behavior where audio signal output is compressed at high levels.");

	bool serialNoise = pokey.IsSerialNoiseEnabled();
	if (ImGui::Checkbox("Serial noise", &serialNoise))
		pokey.SetSerialNoiseEnabled(serialNoise);
	ImGui::SetItemTooltip("Enable audio noise when serial transfers occur.");

	bool speaker = pokey.IsSpeakerFilterEnabled();
	if (ImGui::Checkbox("Simulate console speaker", &speaker))
		pokey.SetSpeakerFilterEnabled(speaker);
	ImGui::SetItemTooltip("Simulate the acoustics of the console speaker.");

	ImGui::SeparatorText("Enabled channels");

	// Primary POKEY channels 1-4
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "%d", i + 1);
		bool ch = pokey.IsChannelEnabled(i);
		if (ImGui::Checkbox(label, &ch))
			pokey.SetChannelEnabled(i, ch);
		if (i < 3) ImGui::SameLine();
	}

	// Secondary POKEY channels (if stereo enabled)
	if (dualPokey) {
		for (int i = 0; i < 4; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "%dR", i + 1);
			bool ch = pokey.IsSecondaryChannelEnabled(i);
			if (ImGui::Checkbox(label, &ch))
				pokey.SetSecondaryChannelEnabled(i, ch);
			if (i < 3) ImGui::SameLine();
		}
	}

	ImGui::Separator();

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Drive Sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);
	ImGui::SetItemTooltip("Simulate the sounds of a real disk drive.");

	bool audioMonitor = sim.IsAudioMonitorEnabled();
	if (ImGui::Checkbox("Audio monitor", &audioMonitor))
		sim.SetAudioMonitorEnabled(audioMonitor);
	ImGui::SetItemTooltip("Display real-time audio output monitor on screen.");

	bool audioScope = sim.IsAudioScopeEnabled();
	if (ImGui::Checkbox("Audio scope", &audioScope))
		sim.SetAudioScopeEnabled(audioScope);

	ImGui::Separator();
	if (ImGui::Button("Host audio options..."))
		state.showAudioOptions = true;
}

// =========================================================================
// Keyboard (matches Windows IDD_CONFIGURE_KEYBOARD)
// =========================================================================

static void RenderKeyboardCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_KEYBOARD
	extern ATUIKeyboardOptions g_kbdOpts;

	ImGui::SeparatorText("Keyboard mode");

	// Maps to Windows Input > Keyboard Mode (Cooked/Raw/Full Scan)
	// Cooked: mbRawKeys=false, mbFullRawKeys=false
	// Raw:    mbRawKeys=true,  mbFullRawKeys=false
	// Full:   mbRawKeys=true,  mbFullRawKeys=true
	static const char *kKeyboardModes[] = { "Cooked", "Raw", "Full Scan" };
	int kbdMode = 0;
	if (g_kbdOpts.mbFullRawKeys) kbdMode = 2;
	else if (g_kbdOpts.mbRawKeys) kbdMode = 1;
	if (ImGui::Combo("##KeyboardMode", &kbdMode, kKeyboardModes, 3)) {
		g_kbdOpts.mbRawKeys = (kbdMode >= 1);
		g_kbdOpts.mbFullRawKeys = (kbdMode == 2);
	}
	ImGui::SetItemTooltip("Control how keys are sent to the emulation.");

	ImGui::SeparatorText("Arrow key mode");

	static const char *kArrowModes[] = {
		"Invert Ctrl", "Auto Ctrl", "Default Ctrl"
	};
	int akm = (int)g_kbdOpts.mArrowKeyMode;
	if (akm < 0 || akm >= 3) akm = 0;
	if (ImGui::Combo("##ArrowKeyMode", &akm, kArrowModes, 3)) {
		g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)akm;
		ATUIInitVirtualKeyMap(g_kbdOpts);
	}
	ImGui::SetItemTooltip("Controls how arrow keys are mapped to the emulated keyboard.");

	ImGui::SeparatorText("Key press mode");

	static const char *kLayoutModes[] = {
		"Natural", "Raw", "Custom"
	};
	int lm = (int)g_kbdOpts.mLayoutMode;
	if (lm < 0 || lm >= 3) lm = 0;
	if (ImGui::Combo("##LayoutMode", &lm, kLayoutModes, 3)) {
		g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)lm;
		ATUIInitVirtualKeyMap(g_kbdOpts);
	}
	ImGui::SetItemTooltip("Select mapping from host to emulated keyboard.");

	ImGui::Separator();

	if (ImGui::Checkbox("Allow SHIFT key to be detected on cold reset", &g_kbdOpts.mbAllowShiftOnColdReset))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Control whether the emulation detects the SHIFT key being held during cold reset.");

	if (ImGui::Checkbox("Enable F1-F4 as 1200XL function keys", &g_kbdOpts.mbEnableFunctionKeys))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Map F1-F4 in the default keyboard layouts to the four function keys on the 1200XL.");

	if (ImGui::Checkbox("Share modifier host keys between keyboard and input maps", &g_kbdOpts.mbAllowInputMapModifierOverlap))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Allow Ctrl/Shift keys to be shared between the keyboard handler and input maps.");

	if (ImGui::Checkbox("Share non-modifier host keys between keyboard and input maps", &g_kbdOpts.mbAllowInputMapOverlap))
		ATUIInitVirtualKeyMap(g_kbdOpts);
	ImGui::SetItemTooltip("Allow the same non-Ctrl/Shift key to be used by both the keyboard handler and input maps.");
}

// =========================================================================
// Disk (matches Windows IDD_CONFIGURE_DISK)
// =========================================================================

static void RenderDiskCategory(ATSimulator &sim) {
	bool accurateTiming = sim.IsDiskAccurateTimingEnabled();
	if (ImGui::Checkbox("Accurate sector timing", &accurateTiming))
		sim.SetDiskAccurateTimingEnabled(accurateTiming);
	ImGui::SetItemTooltip("Emulate the seek times and rotational delays of a real disk drive.");

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Play drive sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);
	ImGui::SetItemTooltip("Simulate the sounds of a real disk drive.");

	bool sectorCounter = sim.IsDiskSectorCounterEnabled();
	if (ImGui::Checkbox("Show sector counter", &sectorCounter))
		sim.SetDiskSectorCounterEnabled(sectorCounter);
	ImGui::SetItemTooltip("During disk access, display the sector number being read or written.");
}

// =========================================================================
// Cassette (matches Windows IDD_CONFIGURE_CASSETTE)
// =========================================================================

static void RenderCassetteCategory(ATSimulator &sim) {
	ATCassetteEmulator& cas = sim.GetCassette();

	ImGui::SeparatorText("Tape setup");

	bool autoBoot = sim.IsCassetteAutoBootEnabled();
	if (ImGui::Checkbox("Auto-boot on startup", &autoBoot))
		sim.SetCassetteAutoBootEnabled(autoBoot);
	ImGui::SetItemTooltip("Automatically hold down the Start button on power-up to boot from tape.");

	bool autoBasicBoot = sim.IsCassetteAutoBasicBootEnabled();
	if (ImGui::Checkbox("Auto-boot BASIC on startup", &autoBasicBoot))
		sim.SetCassetteAutoBasicBootEnabled(autoBasicBoot);
	ImGui::SetItemTooltip("Try to determine if the tape has a BASIC or binary program and auto-start accordingly.");

	bool autoRewind = sim.IsCassetteAutoRewindEnabled();
	if (ImGui::Checkbox("Auto-rewind on startup", &autoRewind))
		sim.SetCassetteAutoRewindEnabled(autoRewind);
	ImGui::SetItemTooltip("Automatically rewind the tape to the beginning on startup.");

	bool loadDataAsAudio = cas.IsLoadDataAsAudioEnabled();
	if (ImGui::Checkbox("Load data as audio", &loadDataAsAudio))
		cas.SetLoadDataAsAudioEnable(loadDataAsAudio);
	ImGui::SetItemTooltip("Play the data track as the audio track.");

	bool randomStart = sim.IsCassetteRandomizedStartEnabled();
	if (ImGui::Checkbox("Randomize starting position", &randomStart))
		sim.SetCassetteRandomizedStartEnabled(randomStart);
	ImGui::SetItemTooltip("Apply a slight jitter to the start position of the tape.");

	ImGui::SeparatorText("Turbo support");

	static const char *kTurboModes[] = {
		"None", "Command Control", "Proceed Sense",
		"Interrupt Sense", "KSO Turbo 2000", "Turbo D",
		"Data Control", "Always"
	};
	int turbo = (int)cas.GetTurboMode();
	if (turbo < 0 || turbo >= 8) turbo = 0;
	if (ImGui::Combo("Turbo mode", &turbo, kTurboModes, 8))
		cas.SetTurboMode((ATCassetteTurboMode)turbo);
	ImGui::SetItemTooltip("Select turbo tape hardware modification to support.");

	static const char *kTurboDecoders[] = {
		"Slope (No Filter)", "Slope (Filter)",
		"Peak (Filter)", "Peak (Balance Lo-Hi)", "Peak (Balance Hi-Lo)"
	};
	int decoder = (int)cas.GetTurboDecodeAlgorithm();
	if (decoder < 0 || decoder >= 5) decoder = 0;
	if (ImGui::Combo("Turbo decoder", &decoder, kTurboDecoders, 5))
		cas.SetTurboDecodeAlgorithm((ATCassetteTurboDecodeAlgorithm)decoder);
	ImGui::SetItemTooltip("Decoding algorithm to apply when decoding turbo data.");

	bool invertTurbo = cas.GetPolarityMode() == kATCassettePolarityMode_Inverted;
	if (ImGui::Checkbox("Invert turbo data", &invertTurbo))
		cas.SetPolarityMode(invertTurbo
			? kATCassettePolarityMode_Inverted
			: kATCassettePolarityMode_Normal);
	ImGui::SetItemTooltip("Invert the polarity of turbo data read by the computer.");

	ImGui::SeparatorText("Direct read filter");

	static const char *kDirectSenseModes[] = {
		"Normal", "Low Speed", "High Speed", "Max Speed"
	};
	int dsm = (int)cas.GetDirectSenseMode();
	if (dsm < 0 || dsm >= 4) dsm = 0;
	if (ImGui::Combo("##DirectReadFilter", &dsm, kDirectSenseModes, 4))
		cas.SetDirectSenseMode((ATCassetteDirectSenseMode)dsm);
	ImGui::SetItemTooltip("Selects the bandwidth of filter used for FSK direct read decoding.");

	ImGui::SeparatorText("Workarounds");

	bool vbiAvoid = cas.IsVBIAvoidanceEnabled();
	if (ImGui::Checkbox("Avoid OS C: random VBI-related errors", &vbiAvoid))
		cas.SetVBIAvoidanceEnabled(vbiAvoid);
	ImGui::SetItemTooltip("Latch cassette data across the start of vertical blank to avoid random read errors.");

	ImGui::SeparatorText("Pre-filtering");

	bool fskComp = cas.GetFSKSpeedCompensationEnabled();
	if (ImGui::Checkbox("Enable FSK speed compensation", &fskComp))
		cas.SetFSKSpeedCompensationEnabled(fskComp);
	ImGui::SetItemTooltip("Correct for speed variation on the tape.");

	bool crosstalk = cas.GetCrosstalkReductionEnabled();
	if (ImGui::Checkbox("Enable crosstalk reduction", &crosstalk))
		cas.SetCrosstalkReductionEnabled(crosstalk);
	ImGui::SetItemTooltip("Reduce crosstalk leakage from the data track into the audio track.");
}

// =========================================================================
// Display (matches Windows IDD_CONFIGURE_DISPLAY)
// =========================================================================

static void RenderDisplayCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_DISPLAY — pointer/indicator settings
	// Note: Filter mode, stretch mode, overscan are View menu items, not here.

	bool autoHide = ATUIGetPointerAutoHide();
	if (ImGui::Checkbox("Auto-hide mouse pointer after short delay", &autoHide))
		ATUISetPointerAutoHide(autoHide);
	ImGui::SetItemTooltip("Automatically hide the mouse pointer after a short delay.");

	bool constrainFS = ATUIGetConstrainMouseFullScreen();
	if (ImGui::Checkbox("Constrain mouse pointer in full-screen mode", &constrainFS))
		ATUISetConstrainMouseFullScreen(constrainFS);
	ImGui::SetItemTooltip("Restrict pointer movement to the emulator window in full-screen mode.");

	bool hideTarget = !ATUIGetTargetPointerVisible();
	if (ImGui::Checkbox("Hide target pointer for absolute mouse input (light pen/gun/tablet)", &hideTarget))
		ATUISetTargetPointerVisible(!hideTarget);
	ImGui::SetItemTooltip("Hide the target reticle pointer for absolute mouse input (light pen/gun/tablet).");

	ImGui::Separator();

	bool indicators = ATUIGetDisplayIndicators();
	if (ImGui::Checkbox("Show indicators", &indicators))
		ATUISetDisplayIndicators(indicators);
	ImGui::SetItemTooltip("Draw on-screen overlays for device status.");

	bool padIndicators = ATUIGetDisplayPadIndicators();
	if (ImGui::Checkbox("Pad bottom margin to reserve space for indicators", &padIndicators))
		ATUISetDisplayPadIndicators(padIndicators);
	ImGui::SetItemTooltip("Move the display up to reserve space for indicators at the bottom.");

	bool padBounds = ATUIGetDrawPadBoundsEnabled();
	if (ImGui::Checkbox("Show tablet/pad bounds", &padBounds))
		ATUISetDrawPadBoundsEnabled(padBounds);
	ImGui::SetItemTooltip("Show a rectangle for the on-screen input area.");

	bool padPointers = ATUIGetDrawPadPointersEnabled();
	if (ImGui::Checkbox("Show tablet/pad pointers", &padPointers))
		ATUISetDrawPadPointersEnabled(padPointers);
	ImGui::SetItemTooltip("Show the location and size of tablet/pad touch points.");
}

// =========================================================================
// Input (matches Windows IDD_CONFIGURE_INPUT)
// =========================================================================

static void RenderInputCategory(ATSimulator &sim) {
	ATPokeyEmulator& pokey = sim.GetPokey();

	bool potNoise = sim.GetPotNoiseEnabled();
	if (ImGui::Checkbox("Enable paddle potentiometer noise", &potNoise))
		sim.SetPotNoiseEnabled(potNoise);
	ImGui::SetItemTooltip("Jitter paddle inputs to simulate a dirty paddle.");

	bool immPots = pokey.IsImmediatePotUpdateEnabled();
	if (ImGui::Checkbox("Use immediate analog updates", &immPots))
		pokey.SetImmediatePotUpdateEnabled(immPots);
	ImGui::SetItemTooltip("Allow paddle position registers to update immediately rather than waiting for scan.");

	bool immLightPen = sim.GetLightPenPort()->GetImmediateUpdateEnabled();
	if (ImGui::Checkbox("Use immediate light pen updates", &immLightPen))
		sim.GetLightPenPort()->SetImmediateUpdateEnabled(immLightPen);
	ImGui::SetItemTooltip("Allow light pen position registers to update immediately.");
}

// =========================================================================
// Ease of Use (matches Windows IDD_CONFIGURE_EASEOFUSE)
// =========================================================================

static void RenderEaseOfUseCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_EASEOFUSE
	uint32 flags = ATUIGetResetFlags();

	bool resetCart = (flags & kATUIResetFlag_CartridgeChange) != 0;
	if (ImGui::Checkbox("Reset when changing cartridges", &resetCart))
		ATUIModifyResetFlag(kATUIResetFlag_CartridgeChange, resetCart);
	ImGui::SetItemTooltip("Reset when adding or removing a cartridge.");

	bool resetVS = (flags & kATUIResetFlag_VideoStandardChange) != 0;
	if (ImGui::Checkbox("Reset when changing video standard", &resetVS))
		ATUIModifyResetFlag(kATUIResetFlag_VideoStandardChange, resetVS);
	ImGui::SetItemTooltip("Reset when changing between NTSC/PAL/SECAM.");

	bool resetBasic = (flags & kATUIResetFlag_BasicChange) != 0;
	if (ImGui::Checkbox("Reset when toggling internal BASIC", &resetBasic))
		ATUIModifyResetFlag(kATUIResetFlag_BasicChange, resetBasic);
	ImGui::SetItemTooltip("Reset when enabling or disabling internal BASIC.");
}

// =========================================================================
// Devices (matches Windows IDD_CONFIGURE_DEVICES)
// =========================================================================

// Device catalog — matches Windows uidevices.cpp CategoryEntry/TreeEntry structure.
// Each entry is { device_tag, display_name }.
struct DeviceCatalogEntry {
	const char *tag;
	const char *displayName;
	const char *helpText;	// tooltip shown on hover in Add Device menu (nullptr = none)
};

struct DeviceCategoryDef {
	const char *categoryName;
	const DeviceCatalogEntry *entries;
	int count;
};

// Device catalog matching Windows uidevices.cpp (all categories and entries).
// Tags that don't have a registered ATDeviceDefinition will appear grayed out.

static const DeviceCatalogEntry kPBIDevices[] = {
	{ "1090",           "1090 80 Column Video Card", "PBI-based 80 column video display card for the 1090 XL expansion system." },
	{ "blackbox",       "Black Box", "Provides SCSI hard disk, RAM disk, parallel printer, and RS-232 serial via PBI port." },
	{ "karinmaxidrive", "Karin Maxi Drive", "PBI disk interface providing access to up to two disk drives." },
	{ "kmkjzide",       "KMK/JZ IDE v1", "PBI-based hard disk interface for parallel ATA devices." },
	{ "kmkjzide2",      "KMK/JZ IDE v2 (IDEPlus 2.0)", "PBI-based ATA interface with expanded firmware and SpartaDOS X." },
	{ "mio",            "MIO", "ICD Multi-I/O: SCSI hard disk, RAM disk, printer, and RS-232 via PBI." },
};

static const DeviceCatalogEntry kCartridgeDevices[] = {
	{ "dragoncart",     "DragonCart", "Adds an Ethernet port. No firmware built-in; networking software must be run separately." },
	{ "multiplexer",    "Multiplexer", "Cartridge allowing computers to share disk drives and printers over a local network." },
	{ "myide-d5xx",     "MyIDE (cartridge)", "IDE adapter attached to cartridge port, using the $D5xx address range." },
	{ "myide2",         "MyIDE-II", "Enhanced MyIDE with CompactFlash interface, hot-swap support, and banked flash memory." },
	{ "rtime8",         "R-Time 8", "Simple cartridge providing a real-time clock. Requires separate Z: handler software." },
	{ "side",           "SIDE", "Cartridge with 512K flash and CompactFlash port." },
	{ "side2",          "SIDE 2", "Enhanced SIDE with improved banking and CompactFlash change detection." },
	{ "side3",          "SIDE 3", "Third-generation SIDE with SD card storage, cartridge emulation, and DMA." },
	{ "slightsid",      "SlightSID", "Cartridge-shaped adapter for the C64's SID sound chip." },
	{ "veronica",       "Veronica", "Cartridge-based 65C816 coprocessor with 128K on-board memory." },
	{ "thepill",        "The Pill", "Write-protects cartridge memory areas to simulate a cartridge using RAM." },
};

static const DeviceCatalogEntry kInternalDevices[] = {
	{ "1450xldisk",     "1450XLD Disk Controller", "Internal PBI-based disk controller in the 1450XLD." },
	{ "1450xldiskfull", "1450XLD Disk Controller (full emulation)", "Internal PBI-based disk controller with full 8040 controller emulation." },
	{ "1450xltongdiskfull", "1450XLD \"TONG\" Disk Controller (full emulation)", "Internal PBI-based disk controller in the TONG 1450XLD variant." },
	{ "warpos",         "APE Warp+ OS 32-in-1", "Allows soft-switching between 32 different OS ROMs." },
	{ "bit3",           "Bit 3 Full-View 80", "80 column video board for the 800, replaces RAM slot 3." },
	{ "covox",          "Covox", "Simple DAC for 8-bit digital sound." },
	{ "rapidus",        "Rapidus Accelerator", "6502/65C816 accelerator at 20MHz with 15MB memory and 512KB flash." },
	{ "soundboard",     "SoundBoard", "Multi-channel wavetable sound with 512K internal memory." },
	{ "myide-d1xx",     "MyIDE (internal)", "IDE adapter attached to internal port, using the $D1xx address range." },
	{ "vbxe",           "VideoBoard XE (VBXE)", "Enhanced video with 512K VRAM, 640x resolution, overlays, and blitter." },
	{ "xelcf",          "XEL-CF CompactFlash Adapter", "CompactFlash at $D1xx with reset strobe." },
	{ "xelcf3",         "XEL-CF3 CompactFlash Adapter", "CompactFlash at $D1xx with reset strobe and swap button." },
};

static const DeviceCatalogEntry kControllerPortDevices[] = {
	{ "computereyes",   "ComputerEyes Video Acquisition System", "Video capture device connecting to joystick ports 1 and 2." },
	{ "corvus",         "Corvus Disk Interface", "External hard drive connected to controller ports 3+4." },
	{ "dongle",         "Joystick port dongle", "Copy-protection dongle with configurable bit mapping." },
	{ "mpp1000e",       "Microbits MPP-1000E Modem", "300 baud modem connecting to joystick port 2." },
	{ "simcovox",       "SimCovox", "Joystick-based Covox device plugging into ports 1 and 2." },
	{ "supersalt",      "SuperSALT Test Assembly", "Test device used with SuperSALT cartridges." },
	{ "xep80",          "XEP80", "External 80-column video output via joystick port." },
};

static const DeviceCatalogEntry kHardDiskDevices[] = {
	{ "harddisk",       "Hard disk", "Hard drive or SSD. Add as sub-device to IDE, CF, SCSI, or SD card parent." },
	{ "hdtempwritefilter", "Temporary write filter", "Allows writes to read-only images by caching in memory." },
	{ "hdvirtfat16",    "Virtual FAT16 hard disk", "Virtual drive from host directory as FAT16 partition (max 256MB)." },
	{ "hdvirtfat32",    "Virtual FAT32 hard disk", "Virtual drive from host directory as FAT32 partition." },
	{ "hdvirtsdfs",     "Virtual SDFS hard disk", "Virtual drive from host directory as SpartaDOS partition." },
};

static const DeviceCatalogEntry kSerialDevices[] = {
	{ "parfilewriter",  "File writer", "Writes all data from a parallel or serial port to a file." },
	{ "loopback",       "Loopback", "Connects transmit and receive lines together for testing." },
	{ "modem",          "Modem", "Hayes compatible modem with TCP/IP connection." },
	{ "netserial",      "Networked serial port", "Network to serial port bridge over TCP/IP." },
	{ "pipeserial",     "Named pipe serial port", "Named pipe to serial port bridge." },
	{ "serialsplitter", "Serial splitter", "Allows different connections for serial port input and output." },
};

static const DeviceCatalogEntry kParallelDevices[] = {
	{ "825",            "825 80-Column Printer", "80 column dot-matrix printer with parallel port." },
	{ "parfilewriter",  "File writer", "Writes all data from a parallel or serial port to a file." },
	{ "par2ser",        "Parallel to serial adapter", "Connects a parallel port output to a serial input." },
};

static const DeviceCatalogEntry kDiskDriveDevices[] = {
	{ "diskdrive810",           "810", "Full 810 emulation with 6507 CPU. Single density only." },
	{ "diskdrive810archiver",   "810 Archiver", "Full 810 Archiver emulation (810 with \"The Chip\")." },
	{ "diskdrivehappy810",      "Happy 810", "Full Happy 810 emulation with track buffering." },
	{ "diskdrive810turbo",      "810 Turbo", "Full 810 Turbo (NCT) emulation with double density." },
	{ "diskdrive815",           "815", "Full 815 dual drive emulation. Double density only, read-only." },
	{ "diskdrive1050",          "1050", "Full 1050 emulation. Single and enhanced density." },
	{ "diskdrive1050duplicator","1050 Duplicator", "Full 1050 Duplicator emulation." },
	{ "diskdriveusdoubler",     "US Doubler", "Enhanced 1050 with true double density and high speed." },
	{ "diskdrivespeedy1050",    "Speedy 1050", "Enhanced 1050 with double density, track buffering, and high speed." },
	{ "diskdrivespeedyxf",      "Speedy XF", "Modified XF551 with 65C02, 64K ROM, and 32K RAM." },
	{ "diskdrivehappy1050",     "Happy 1050", "Full Happy 1050 emulation." },
	{ "diskdrivesuperarchiver", "Super Archiver", "Full Super Archiver emulation." },
	{ "diskdrivesuperarchiverbw","Super Archiver w/BitWriter", "Super Archiver with raw write capability." },
	{ "diskdrivetoms1050",      "TOMS 1050", "Full TOMS 1050 emulation." },
	{ "diskdrivetygrys1050",    "Tygrys 1050", "Full Tygrys 1050 emulation." },
	{ "diskdrive1050turbo",     "1050 Turbo", "Full 1050 Turbo (Bernhard Engl) emulation." },
	{ "diskdrive1050turboii",   "1050 Turbo II", "Full 1050 Turbo II emulation." },
	{ "diskdriveisplate",       "I.S. Plate", "Full I.S. Plate emulation with double density and track buffering." },
	{ "diskdriveindusgt",       "Indus GT", "Full Indus GT emulation with Z80 CPU and RamCharger." },
	{ "diskdrivexf551",         "XF551", "Full XF551 emulation with 8048 CPU. Supports double-sided." },
	{ "diskdriveatr8000",       "ATR8000", "Full ATR8000 emulation with Z80, up to 4 drives, printer, and serial." },
	{ "diskdrivepercom",        "Percom RFD-40S1", "Full Percom RFD-40S1 emulation with 6809 CPU." },
	{ "diskdrivepercomat",      "Percom AT-88S1", "Full Percom AT-88S1 emulation (without printer interface)." },
	{ "diskdrivepercomatspd",   "Percom AT88-SPD", "Full Percom AT88-SPD emulation (with printer interface)." },
	{ "diskdriveamdc",          "Amdek AMDC-I/II", "Full Amdek AMDC-I/II emulation with 6809 CPU." },
};

static const DeviceCatalogEntry kSIODevices[] = {
	{ "820",            "820 40-Column Printer", "Basic 40 column dot-matrix printer." },
	{ "820full",        "820 40-Column Printer (full emulation)", "Full 6507 emulation with dot matrix rendering." },
	{ "835",            "835 Modem", "300 baud SIO modem." },
	{ "835full",        "835 Modem (full emulation)", "Full 8048 hardware emulation." },
	{ "850",            "850 Interface Module", "Four RS-232 serial ports and printer port." },
	{ "850full",        "850 Interface Module (full emulation)", "Full 850 hardware emulation with 6502 controller." },
	{ "1020",           "1020 Color Printer", "Four-color plotter with 820-compatible protocol." },
	{ "1025",           "1025 80-Column Printer", "80 column dot-matrix with double-width and condensed modes." },
	{ "1025full",       "1025 80-Column Printer (full emulation)", "Full 1025 hardware emulation." },
	{ "1029",           "1029 80-Column Printer", "80 column dot-matrix with graphics support." },
	{ "1029full",       "1029 80-Column Printer (full emulation)", "Full 1029 hardware emulation." },
	{ "1030",           "1030 Modem", "300 baud SIO modem with T: handler." },
	{ "1030full",       "1030 Modem (full emulation)", "Full 8050 hardware emulation with auto-boot firmware." },
	{ "midimate",       "MidiMate", "SIO-based MIDI adapter linked to host MIDI." },
	{ "pclink",         "PCLink", "PC-based file server via SIO. Requires SpartaDOS X Toolkit handler." },
	{ "pocketmodem",    "Pocket Modem", "SIO modem capable of 110-500 baud." },
	{ "rverter",        "R-Verter", "SIO to RS-232 adapter cable. Requires R: handler from disk." },
	{ "sdrive",         "SDrive", "Hardware disk emulator using SD card images." },
	{ "sioserial",      "SIO serial adapter", "SIO bus to traditional serial port adapter (SIO2PC-like)." },
	{ "sio2sd",         "SIO2SD", "Hardware disk emulator using SD card images." },
	{ "sioclock",       "SIO Real-Time Clock", "Implements APE, AspeQt, and SIO2USB RTC protocols." },
	{ "sx212",          "SX212 Modem", "1200 baud SIO modem." },
	{ "testsiopoll3",   "SIO Type 3 Poll Test Device", "Test device for XL/XE boot-time handler auto-load." },
	{ "testsiopoll4",   "SIO Type 4 Poll Test Device", "Test device for XL/XE on-demand handler auto-load." },
	{ "testsiohs",      "SIO High Speed Test Device", "Test device for ultra-high speed SIO with external clock." },
	{ "xm301",          "XM301 Modem", "300 baud SIO modem with auto-answer and audio." },
};

static const DeviceCatalogEntry kHLEDevices[] = {
	{ "hostfs",         "Host device (H:)", "Access host files via H: device. Can also be installed as D:." },
	{ "printer",        "Printer (P:)", "Routes P: output to the Printer Output window." },
	{ "browser",        "Browser (B:)", "Parses HTTP/HTTPS URLs written to B: and opens in browser." },
};

static const DeviceCatalogEntry kVideoSourceDevices[] = {
	{ "videogenerator", "Video generator", "Generates a static image frame for composite video input." },
	{ "videostillimage","Video still image", "Generates a still image frame from an image file." },
};

static const DeviceCatalogEntry kAddOnDevices[] = {
	{ "blackboxfloppy", "Black Box Floppy Board", "Adds parallel bus floppy drive support to the Black Box." },
};

static const DeviceCatalogEntry kOtherDevices[] = {
	{ "custom",         "Custom device", "Custom device based on a .atdevice description file." },
};

static const DeviceCategoryDef kDeviceCategories[] = {
	{ "PBI devices",          kPBIDevices,           (int)(sizeof(kPBIDevices)/sizeof(kPBIDevices[0])) },
	{ "Cartridge devices",    kCartridgeDevices,     (int)(sizeof(kCartridgeDevices)/sizeof(kCartridgeDevices[0])) },
	{ "Internal devices",     kInternalDevices,      (int)(sizeof(kInternalDevices)/sizeof(kInternalDevices[0])) },
	{ "Controller port",      kControllerPortDevices,(int)(sizeof(kControllerPortDevices)/sizeof(kControllerPortDevices[0])) },
	{ "Hard disks",           kHardDiskDevices,      (int)(sizeof(kHardDiskDevices)/sizeof(kHardDiskDevices[0])) },
	{ "Serial devices",       kSerialDevices,        (int)(sizeof(kSerialDevices)/sizeof(kSerialDevices[0])) },
	{ "Parallel port",        kParallelDevices,      (int)(sizeof(kParallelDevices)/sizeof(kParallelDevices[0])) },
	{ "Disk drives",          kDiskDriveDevices,     (int)(sizeof(kDiskDriveDevices)/sizeof(kDiskDriveDevices[0])) },
	{ "SIO bus devices",      kSIODevices,           (int)(sizeof(kSIODevices)/sizeof(kSIODevices[0])) },
	{ "HLE devices",          kHLEDevices,           (int)(sizeof(kHLEDevices)/sizeof(kHLEDevices[0])) },
	{ "Video source devices", kVideoSourceDevices,   (int)(sizeof(kVideoSourceDevices)/sizeof(kVideoSourceDevices[0])) },
	{ "Add-on devices",       kAddOnDevices,         (int)(sizeof(kAddOnDevices)/sizeof(kAddOnDevices[0])) },
	{ "Other devices",        kOtherDevices,         (int)(sizeof(kOtherDevices)/sizeof(kOtherDevices[0])) },
};
static const int kNumDeviceCategories = (int)(sizeof(kDeviceCategories)/sizeof(kDeviceCategories[0]));

static int g_selectedDeviceIndex = -1;

static void RenderDevicesCategory(ATSimulator &sim) {
	ATDeviceManager *devMgr = sim.GetDeviceManager();
	if (!devMgr) {
		ImGui::TextDisabled("Device manager not available.");
		return;
	}

	ImGui::SeparatorText("Attached devices");

	// Build hierarchical list of devices (matching Windows tree view)
	struct DevEntry {
		VDStringA name;
		VDStringA tag;
		VDStringA configTag;
		VDStringA blurb;
		IATDevice *pDev;
		ATDeviceFirmwareStatus fwStatus;
		bool hasFwStatus;
		bool hasSettings;
		int depth;		// indentation level (0 = top-level)
		sint32 busId;	// bus ID for XCmd context (-1 for top-level)
	};

	vdvector<DevEntry> devices;

	// Helper: add a device and recursively add its children
	struct DevTreeBuilder {
		static void AddDevice(vdvector<DevEntry>& devices, IATDevice *dev, int depth, sint32 busId) {
			ATDeviceInfo info;
			dev->GetDeviceInfo(info);

			DevEntry entry;
			entry.name = VDTextWToU8(VDStringW(info.mpDef->mpName));
			entry.tag = info.mpDef->mpTag ? info.mpDef->mpTag : "";
			entry.configTag = info.mpDef->mpConfigTag ? info.mpDef->mpConfigTag : "";
			entry.pDev = dev;
			entry.hasFwStatus = false;
			entry.fwStatus = ATDeviceFirmwareStatus::OK;
			entry.hasSettings = (info.mpDef->mpConfigTag != nullptr);
			entry.depth = depth;
			entry.busId = busId;

			VDStringW blurbW;
			dev->GetSettingsBlurb(blurbW);
			if (!blurbW.empty())
				entry.blurb = VDTextWToU8(blurbW);

			IATDeviceFirmware *fwIface = vdpoly_cast<IATDeviceFirmware *>(dev);
			if (fwIface) {
				entry.hasFwStatus = true;
				entry.fwStatus = fwIface->GetFirmwareStatus();
			}

			devices.push_back(std::move(entry));

			// Enumerate child devices via IATDeviceParent/IATDeviceBus
			IATDeviceParent *parent = vdpoly_cast<IATDeviceParent *>(dev);
			if (parent) {
				for (uint32 bi = 0; ; ++bi) {
					sint32 childBusId = parent->GetDeviceBusIdByIndex(bi);
					if (childBusId < 0) break;
					IATDeviceBus *bus = parent->GetDeviceBusById(childBusId);
					if (!bus) continue;

					vdfastvector<IATDevice *> children;
					bus->GetChildDevices(children);
					for (IATDevice *child : children)
						AddDevice(devices, child, depth + 1, childBusId);
				}
			}
		}
	};

	for (IATDevice *dev : devMgr->GetDevices(true, true, true))
		DevTreeBuilder::AddDevice(devices, dev, 0, -1);

	// Clamp selection
	if (g_selectedDeviceIndex >= (int)devices.size())
		g_selectedDeviceIndex = (int)devices.size() - 1;

	float listHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y * 0.45f);

	if (devices.empty()) {
		ImGui::TextDisabled("No external devices attached.");
	} else {
		if (ImGui::BeginTable("##DeviceList", 3,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
				ImVec2(0, listHeight))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)devices.size(); ++i) {
				const auto &dev = devices[i];
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableNextColumn();
				// Indent child devices to show hierarchy
				if (dev.depth > 0)
					ImGui::Indent(dev.depth * 16.0f);
				bool selected = (i == g_selectedDeviceIndex);
				if (ImGui::Selectable(dev.name.c_str(), selected,
						ImGuiSelectableFlags_AllowDoubleClick))
				{
					g_selectedDeviceIndex = i;
					// Double-click opens settings (matching Windows)
					if (ImGui::IsMouseDoubleClicked(0) && dev.hasSettings)
						ATUIOpenDeviceConfig(dev.pDev, devMgr);
				}
				// Show settings blurb next to device name
				if (!dev.blurb.empty()) {
					ImGui::SameLine();
					ImGui::TextDisabled("(%s)", dev.blurb.c_str());
				}

				// Right-click context menu with extended commands
				if (ImGui::BeginPopupContextItem("##devctx")) {
					g_selectedDeviceIndex = i;
					if (dev.hasSettings && ImGui::MenuItem("Settings..."))
						ATUIOpenDeviceConfig(dev.pDev, devMgr);
					if (ImGui::MenuItem("Remove")) {
						ATUICloseDeviceConfigFor(dev.pDev);
						devMgr->RemoveDevice(dev.pDev);
						g_selectedDeviceIndex = -1;
						ImGui::EndPopup();
						ImGui::PopID();
						ImGui::EndTable();
						return;
					}

					// Device-specific extended commands
					auto xcmds = devMgr->GetExtendedCommandsForDevice(dev.pDev, dev.busId);
					if (!xcmds.empty()) {
						ImGui::Separator();
						// Sort by display name (matching Windows)
						vdvector<int> xcmdOrder;
						xcmdOrder.resize(xcmds.size());
						for (int xi = 0; xi < (int)xcmds.size(); ++xi) xcmdOrder[xi] = xi;
						vdvector<ATDeviceXCmdInfo> xcmdInfos;
						xcmdInfos.reserve(xcmds.size());
						for (auto *xcmd : xcmds) xcmdInfos.push_back(xcmd->GetInfo());
						std::sort(xcmdOrder.begin(), xcmdOrder.end(), [&](int a, int b) {
							return xcmdInfos[a].mDisplayName.comparei(xcmdInfos[b].mDisplayName) < 0;
						});
						for (int xi : xcmdOrder) {
							VDStringA label = VDTextWToU8(xcmdInfos[xi].mDisplayName);
							if (ImGui::MenuItem(label.c_str())) {
								xcmds[xi]->Invoke(*devMgr, dev.pDev, dev.busId);
							}
						}
					}
					ImGui::EndPopup();
				}

				if (dev.depth > 0)
					ImGui::Unindent(dev.depth * 16.0f);

				ImGui::TableNextColumn();
				if (dev.hasFwStatus) {
					switch (dev.fwStatus) {
						case ATDeviceFirmwareStatus::OK:
							ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "OK");
							break;
						case ATDeviceFirmwareStatus::Missing:
							ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "FW Missing");
							break;
						case ATDeviceFirmwareStatus::Invalid:
							ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "FW Invalid");
							break;
					}
				} else {
					ImGui::TextDisabled("--");
				}

				ImGui::TableNextColumn();
				if (ImGui::SmallButton("Remove")) {
					ATUICloseDeviceConfigFor(dev.pDev);
					devMgr->RemoveDevice(dev.pDev);
					g_selectedDeviceIndex = -1;
					ImGui::PopID();
					ImGui::EndTable();
					return; // Device list invalidated, re-render next frame
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	// Add Device button with category popup
	if (ImGui::Button("Add Device..."))
		ImGui::OpenPopup("AddDeviceMenu");

	if (ImGui::BeginPopup("AddDeviceMenu")) {
		for (int ci = 0; ci < kNumDeviceCategories; ++ci) {
			const auto &cat = kDeviceCategories[ci];
			if (ImGui::BeginMenu(cat.categoryName)) {
				// Sort entries alphabetically by display name (matching Windows)
				vdvector<int> sortedIdx;
				sortedIdx.resize(cat.count);
				for (int i = 0; i < cat.count; ++i) sortedIdx[i] = i;
				std::sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
					return strcasecmp(cat.entries[a].displayName, cat.entries[b].displayName) < 0;
				});

				for (int si = 0; si < cat.count; ++si) {
					const auto &entry = cat.entries[sortedIdx[si]];
					const ATDeviceDefinition *def = devMgr->GetDeviceDefinition(entry.tag);
					if (ImGui::MenuItem(entry.displayName, nullptr, false, def != nullptr)) {
						if (def) {
							try {
								ATPropertySet pset;
								devMgr->AddDevice(entry.tag, pset);
							} catch (...) {
								fprintf(stderr, "[AltirraSDL] Failed to add device: %s\n",
									entry.tag);
							}
						}
					}
					if (entry.helpText)
						ImGui::SetItemTooltip("%s", entry.helpText);
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	bool hasSelection = (g_selectedDeviceIndex >= 0 && g_selectedDeviceIndex < (int)devices.size());
	if (ImGui::Button("Remove") && hasSelection) {
		ATUICloseDeviceConfigFor(devices[g_selectedDeviceIndex].pDev);
		devMgr->RemoveDevice(devices[g_selectedDeviceIndex].pDev);
		g_selectedDeviceIndex = -1;
	}

	ImGui::SameLine();
	bool canSettings = hasSelection && devices[g_selectedDeviceIndex].hasSettings;
	ImGui::BeginDisabled(!canSettings);
	if (ImGui::Button("Settings...") && canSettings) {
		ATUIOpenDeviceConfig(devices[g_selectedDeviceIndex].pDev, devMgr);
	}
	ImGui::EndDisabled();

	// Render device config dialog if open
	ATUIRenderDeviceConfig(devMgr);
}

// =========================================================================
// Media Defaults (matches Windows IDD_CONFIGURE_MEDIADEFAULTS)
// =========================================================================

static void RenderMediaDefaultsCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Default write mode");
	ImGui::TextWrapped("Controls the default write mode used when mounting new disk or tape images.");

	static const char *kWriteModeLabels[] = {
		"Read Only", "Virtual R/W (Safe)", "Virtual R/W", "Read/Write"
	};
	static const ATMediaWriteMode kWriteValues[] = {
		kATMediaWriteMode_RO, kATMediaWriteMode_VRWSafe,
		kATMediaWriteMode_VRW, kATMediaWriteMode_RW,
	};
	int wmIdx = 0;
	for (int i = 0; i < 4; ++i)
		if (kWriteValues[i] == g_ATOptions.mDefaultWriteMode) { wmIdx = i; break; }
	if (ImGui::Combo("Write mode", &wmIdx, kWriteModeLabels, 4)) {
		ATOptions prev = g_ATOptions;
		g_ATOptions.mDefaultWriteMode = kWriteValues[wmIdx];
		if (g_ATOptions != prev) {
			g_ATOptions.mbDirty = true;
			ATOptionsRunUpdateCallbacks(&prev);
			ATOptionsSave();
		}
	}
	ImGui::SetItemTooltip("Selects the default write mode when media is mounted.");
}

// =========================================================================
// Enhanced Text (matches Windows IDD_CONFIGURE_ENHANCEDTEXT)
// =========================================================================

static void RenderEnhancedTextCategory(ATSimulator &) {
	ImGui::SeparatorText("Enhanced text output");

	static const char *kModes[] = { "None", "Hardware", "Software" };
	ATUIEnhancedTextMode mode = ATUIGetEnhancedTextMode();
	int modeIdx = (int)mode;
	if (modeIdx < 0 || modeIdx >= 3) modeIdx = 0;
	if (ImGui::Combo("Mode", &modeIdx, kModes, 3))
		ATUISetEnhancedTextMode((ATUIEnhancedTextMode)modeIdx);

	ImGui::TextWrapped(
		"Hardware mode uses the video display for text rendering.\n"
		"Software mode renders text independently of video output.");
}

// =========================================================================
// Caption (matches Windows IDD_CONFIGURE_CAPTION)
// =========================================================================

static void RenderCaptionCategory(ATSimulator &) {
	ImGui::SeparatorText("Window caption template");

	ImGui::TextWrapped(
		"Customize the window title bar. Available variables:\n"
		"  $(profile) - current profile name\n"
		"  $(hardware) - hardware mode\n"
		"  $(video) - video standard\n"
		"  $(speed) - speed setting\n"
		"  $(fps) - frames per second");

	// Sync buffer from accessor on first render and when not actively editing
	static char captionBuf[256] = {};
	static bool editing = false;
	if (!editing) {
		const char *tmpl = ATUIGetWindowCaptionTemplate();
		if (tmpl) {
			strncpy(captionBuf, tmpl, sizeof(captionBuf) - 1);
			captionBuf[sizeof(captionBuf) - 1] = 0;
		}
	}

	if (ImGui::InputText("Template", captionBuf, sizeof(captionBuf))) {
		ATUISetWindowCaptionTemplate(captionBuf);
		editing = true;
	}
	if (!ImGui::IsItemActive())
		editing = false;

	if (ImGui::Button("Reset to Default")) {
		captionBuf[0] = 0;
		ATUISetWindowCaptionTemplate("");
		editing = false;
	}
}

// =========================================================================
// Workarounds (matches Windows IDD_CONFIGURE_WORKAROUNDS)
// =========================================================================

static void RenderWorkaroundsCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Directory polling");

	bool poll = g_ATOptions.mbPollDirectories;
	if (ImGui::Checkbox("Poll directories for changes (H: device)", &poll)) {
		ATOptions prev = g_ATOptions;
		g_ATOptions.mbPollDirectories = poll;
		if (g_ATOptions != prev) {
			g_ATOptions.mbDirty = true;
			ATOptionsRunUpdateCallbacks(&prev);
			ATOptionsSave();
		}
	}

	ImGui::TextWrapped(
		"When enabled, the H: device will periodically check for external "
		"changes to host directories. Disable if experiencing performance "
		"issues with large directories.");
}

// =========================================================================
// Compat DB (matches Windows IDD_CONFIGURE_COMPATDB)
// =========================================================================

static void RenderCompatDBCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Compatibility warnings");

	bool compatEnable = g_ATOptions.mbCompatEnable;
	if (ImGui::Checkbox("Show compatibility warnings", &compatEnable)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbCompatEnable = compatEnable;
		if (g_ATOptions != prev) {
			g_ATOptions.mbDirty = true;
			ATOptionsRunUpdateCallbacks(&prev);
			ATOptionsSave();
		}
	}
	ImGui::SetItemTooltip("If enabled, detect and warn about compatibility issues with loaded titles.");

	ImGui::SeparatorText("Database sources");

	bool compatInternal = g_ATOptions.mbCompatEnableInternalDB;
	if (ImGui::Checkbox("Use internal database", &compatInternal)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbCompatEnableInternalDB = compatInternal;
		if (g_ATOptions != prev) {
			g_ATOptions.mbDirty = true;
			ATOptionsRunUpdateCallbacks(&prev);
			ATOptionsSave();
		}
	}
	ImGui::SetItemTooltip("Use built-in compatibility database.");

	bool compatExternal = g_ATOptions.mbCompatEnableExternalDB;
	if (ImGui::Checkbox("Use external database", &compatExternal)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbCompatEnableExternalDB = compatExternal;
		if (g_ATOptions != prev) {
			g_ATOptions.mbDirty = true;
			ATOptionsRunUpdateCallbacks(&prev);
			ATOptionsSave();
		}
	}
	ImGui::SetItemTooltip("Use compatibility database in external file.");

	if (compatExternal) {
		VDStringA pathU8 = VDTextWToU8(g_ATOptions.mCompatExternalDBPath);
		char pathBuf[512];
		strncpy(pathBuf, pathU8.c_str(), sizeof(pathBuf) - 1);
		pathBuf[sizeof(pathBuf) - 1] = 0;

		ImGui::SetNextItemWidth(-100);
		if (ImGui::InputText("##CompatPath", pathBuf, sizeof(pathBuf))) {
			ATOptions prev(g_ATOptions);
			g_ATOptions.mCompatExternalDBPath = VDTextU8ToW(VDStringA(pathBuf));
			if (g_ATOptions != prev) {
				g_ATOptions.mbDirty = true;
				ATOptionsRunUpdateCallbacks(&prev);
				ATOptionsSave();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Browse...")) {
			static const SDL_DialogFileFilter filter = {
				"Compatibility Database", "*.atcpengine"
			};
			SDL_ShowOpenFileDialog(
				[](void *, const char * const *filelist, int) {
					if (filelist && filelist[0]) {
						// Callback may be called from a different thread on
						// some platforms.  We push a deferred action to apply
						// the path on the main thread.
						ATUIPushDeferred(kATDeferred_SetCompatDBPath, filelist[0]);
					}
				},
				nullptr, nullptr, &filter, 1, nullptr, false);
		}
	}

	ImGui::Spacing();
	if (ImGui::Button("Unmute all warnings"))
		ATCompatUnmuteAllTitles();
	ImGui::SetItemTooltip("Re-enable compatibility warnings for all titles that were previously muted.");
}

// =========================================================================
// Error Handling (matches Windows IDD_CONFIGURE_ERRORS)
// =========================================================================

static void RenderErrorHandlingCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Error handling mode");
	ImGui::TextWrapped("Controls what happens when a program triggers an emulated error.");

	static const char *kErrorModeLabels[] = {
		"Show error dialog (default)",
		"Break into the debugger",
		"Pause the emulation",
		"Cold reset the emulation",
	};

	int errIdx = (int)g_ATOptions.mErrorMode;
	if (errIdx < 0 || errIdx >= kATErrorModeCount) errIdx = 0;
	if (ImGui::Combo("Error mode", &errIdx, kErrorModeLabels, kATErrorModeCount)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mErrorMode = (ATErrorMode)errIdx;
		if (g_ATOptions != prev) {
			g_ATOptions.mbDirty = true;
			ATOptionsRunUpdateCallbacks(&prev);
			ATOptionsSave();
		}
	}
}

// =========================================================================
// Flash (matches Windows IDD_CONFIGURE_FLASH)
// =========================================================================

static void RenderFlashCategory(ATSimulator &) {
	ImGui::SeparatorText("Flash chip types");

	static const char *kSICFlashChips[] = {
		"Am29F040B (64K sectors)",
		"SSF39SF040 (4K sectors)",
		"MX29F040 (64K sectors)"
	};
	static const char *kSICFlashIds[] = {
		"Am29F040B", "SST39SF040", "MX29F040"
	};

	static const char *kMaxflash1MbChips[] = {
		"Am29F010 (16K sectors)",
		"M29F010B (16K sectors)",
		"SST39SF010 (4K sectors)"
	};
	static const char *kMaxflash1MbIds[] = {
		"Am29F010", "M29F010B", "SST39SF010"
	};

	static const char *kMaxflash8MbChips[] = {
		"Am29F040B (64K sectors)",
		"BM29F040 (64K sectors)",
		"HY29F040A (64K sectors)",
		"SST39SF040 (4K sectors)"
	};
	static const char *kMaxflash8MbIds[] = {
		"Am29F040B", "BM29F040", "HY29F040A", "SST39SF040"
	};

	static const char *kU1MBChips[] = {
		"A29040 (64K sectors)",
		"SSF39SF040 (4K sectors)",
		"Am29F040B (64K sectors)",
		"BM29F040 (64K sectors)"
	};
	static const char *kU1MBIds[] = {
		"A29040", "SST39SF040", "Am29F040B", "BM29F040"
	};

	auto doCombo = [](const char *label, const char **displayNames, const char **ids, int count,
		VDStringA &option)
	{
		int sel = 0;
		for (int i = 0; i < count; ++i) {
			if (option == ids[i]) { sel = i; break; }
		}
		if (ImGui::Combo(label, &sel, displayNames, count)) {
			if (sel >= 0 && sel < count) {
				ATOptions prev(g_ATOptions);
				option = ids[sel];
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
				}
			}
		}
	};

	doCombo("SIC! flash", kSICFlashChips, kSICFlashIds, 3, g_ATOptions.mSICFlashChip);
	ImGui::SetItemTooltip("Sets the flash chip used for SIC! cartridges.");

	doCombo("MaxFlash 1Mbit flash", kMaxflash1MbChips, kMaxflash1MbIds, 3,
		g_ATOptions.mMaxflash1MbFlashChip);
	ImGui::SetItemTooltip("Sets the flash chip used for MaxFlash 1Mbit cartridges.");

	doCombo("MaxFlash 8Mbit flash", kMaxflash8MbChips, kMaxflash8MbIds, 4,
		g_ATOptions.mMaxflash8MbFlashChip);
	ImGui::SetItemTooltip("Sets the flash chip used for MaxFlash 8Mbit cartridges.");

	doCombo("U1MB flash", kU1MBChips, kU1MBIds, 4, g_ATOptions.mU1MBFlashChip);
	ImGui::SetItemTooltip("Sets the flash chip used for Ultimate1MB.");
}

// =========================================================================
// System Configuration window — paged dialog with hierarchical sidebar
// Matches Windows tree: Computer, Outputs, Peripherals, Media, Emulator
// =========================================================================

struct TreeEntry {
	const char *label;
	int catId;       // -1 = header only (non-selectable)
	int indent;      // 0 = top-level header, 1 = leaf
};

static const TreeEntry kTreeEntries[] = {
	// Top-level pages (before categories, matches Windows)
	{ "Overview",       kCat_Overview,  1 },
	{ "Recommendations", kCat_Recommendations, 1 },
	// Computer
	{ "Computer",       -1,             0 },
	{ "System",         kCat_System,    1 },
	{ "CPU",            kCat_CPU,       1 },
	{ "Firmware",       kCat_Firmware,  1 },
	{ "Memory",         kCat_Memory,    1 },
	{ "Acceleration",   kCat_Acceleration, 1 },
	{ "Speed",          kCat_Speed,     1 },
	{ "Boot",           kCat_Boot,      1 },
	// Outputs
	{ "Outputs",        -1,             0 },
	{ "Video",          kCat_Video,     1 },
	{ "Enhanced Text",  kCat_EnhancedText, 1 },
	{ "Audio",          kCat_Audio,     1 },
	// Peripherals
	{ "Peripherals",    -1,             0 },
	{ "Devices",        kCat_Devices,   1 },
	{ "Keyboard",       kCat_Keyboard,  1 },
	// Media
	{ "Media",          -1,             0 },
	{ "Defaults",       kCat_MediaDefaults, 1 },
	{ "Disk",           kCat_Disk,      1 },
	{ "Cassette",       kCat_Cassette,  1 },
	{ "Flash",          kCat_Flash,     1 },
	// Emulator
	{ "Emulator",       -1,             0 },
	{ "Compat DB",      kCat_CompatDB,  1 },
	{ "Display",        kCat_Display,   1 },
	{ "Ease of Use",    kCat_EaseOfUse, 1 },
	{ "Error Handling", kCat_ErrorHandling, 1 },
	{ "Input",          kCat_Input,     1 },
	{ "Window Caption", kCat_Caption,   1 },
	{ "Workarounds",    kCat_Workarounds, 1 },
};
static const int kNumTreeEntries = sizeof(kTreeEntries) / sizeof(kTreeEntries[0]);

void ATUIRenderSystemConfig(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Configure System", &state.showSystemConfig, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showSystemConfig = false;
		ImGui::End();
		return;
	}

	// Reserve space at bottom for OK button
	float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

	// Left sidebar — tree hierarchy
	ImGui::BeginChild("##SysCfgTree", ImVec2(150, -footerHeight), ImGuiChildFlags_Borders);
	for (int i = 0; i < kNumTreeEntries; ++i) {
		const TreeEntry& te = kTreeEntries[i];
		if (te.indent == 0) {
			// Category header (non-selectable)
			ImGui::Spacing();
			ImGui::TextDisabled("%s", te.label);
		} else {
			// Leaf item (selectable)
			ImGui::Indent(8.0f);
			if (ImGui::Selectable(te.label, state.systemConfigCategory == te.catId))
				state.systemConfigCategory = te.catId;
			ImGui::Unindent(8.0f);
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Right content
	ImGui::BeginChild("##SysCfgContent", ImVec2(0, -footerHeight));
	switch (state.systemConfigCategory) {
	case kCat_Overview:       RenderOverviewCategory(sim); break;
	case kCat_Recommendations: RenderRecommendationsCategory(sim); break;
	case kCat_System:         RenderSystemCategory(sim); break;
	case kCat_CPU:            RenderCPUCategory(sim); break;
	case kCat_Firmware:       RenderFirmwareCategory(sim); break;
	case kCat_Memory:         RenderMemoryCategory(sim); break;
	case kCat_Acceleration:   RenderAccelerationCategory(sim); break;
	case kCat_Speed:          RenderSpeedCategory(sim); break;
	case kCat_Boot:           RenderBootCategory(sim); break;
	case kCat_Video:          RenderVideoCategory(sim); break;
	case kCat_EnhancedText:   RenderEnhancedTextCategory(sim); break;
	case kCat_Audio:          RenderAudioCategory(sim, state); break;
	case kCat_Devices:        RenderDevicesCategory(sim); break;
	case kCat_Keyboard:       RenderKeyboardCategory(sim); break;
	case kCat_MediaDefaults:  RenderMediaDefaultsCategory(sim); break;
	case kCat_Disk:           RenderDiskCategory(sim); break;
	case kCat_Cassette:       RenderCassetteCategory(sim); break;
	case kCat_Flash:          RenderFlashCategory(sim); break;
	case kCat_CompatDB:       RenderCompatDBCategory(sim); break;
	case kCat_Display:        RenderDisplayCategory(sim); break;
	case kCat_EaseOfUse:      RenderEaseOfUseCategory(sim); break;
	case kCat_ErrorHandling:  RenderErrorHandlingCategory(sim); break;
	case kCat_Input:          RenderInputCategory(sim); break;
	case kCat_Caption:        RenderCaptionCategory(sim); break;
	case kCat_Workarounds:    RenderWorkaroundsCategory(sim); break;
	}
	ImGui::EndChild();

	// OK button — matches Windows DEFPUSHBUTTON "OK"
	ImGui::Separator();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showSystemConfig = false;

	ImGui::End();
}
