//	AltirraSDL - System Configuration pages (split from ui_system.cpp, Phase 2i)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>
#include <vd2/system/unknown.h>
#include <vd2/system/file.h>

#include "ui_main.h"
#include "ui_system_internal.h"
#include "simulator.h"

#include "netplay/netplay_glue.h"
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
#include "autosavemanager.h"
#include "debugger.h"
#include "settings.h"
#include <at/atnativeui/genericdialog.h>
#include <at/atui/uimanager.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "logging.h"

extern ATSimulator g_sim;
extern ATUIManager g_ATUIManager;
void ATUIUpdateSpeedTiming();
void ATUIResizeDisplay();
void ATSyncCPUHistoryState();

// =========================================================================
// Overview (matches Windows IDD_CONFIGURE_OVERVIEW — system config summary)
// =========================================================================

void RenderOverviewCategory(ATSimulator &sim) {
	ImGui::SeparatorText("System Configuration Overview");

	// Base system
	const char *vsName = "?";
	switch (sim.GetVideoStandard()) {
		case kATVideoStandard_NTSC:   vsName = "NTSC"; break;
		case kATVideoStandard_NTSC50: vsName = "NTSC50"; break;
		case kATVideoStandard_PAL:    vsName = "PAL"; break;
		case kATVideoStandard_PAL60:  vsName = "PAL60"; break;
		case kATVideoStandard_SECAM:  vsName = "SECAM"; break;
		default: break;
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
		default: break;
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
		default: break;
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

void RenderRecommendationsCategory(ATSimulator &sim) {
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

void RenderSystemCategory(ATSimulator &sim) {
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

void RenderCPUCategory(ATSimulator &sim) {
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

void RenderMemoryCategory(ATSimulator &sim) {
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

	// Matches Windows System.HighMemBanks menu items (cmds.cpp:927-933).
	// Bank counts map to (banks * 64K) of extended memory above bank 0:
	//   -1 = Auto, 0 = None, 1 = 64K, 3 = 192K, 15 = 960K, 63 = 4032K, 255 = 16320K.
	// 65C816 exposes banks 1..255 (256 total banks including bank 0), so
	// 255 is the canonical maximum — ATSimulator::SetHighMemoryBanks
	// clamps anything above 255. The previous value 256 was silently
	// clamped to 255, then the combo lookup failed to match 256 and the
	// selector snapped back to "Auto".
	static const char *kHighMemLabels[] = {
		"Auto", "None", "64K", "192K", "960K", "4032K", "16320K"
	};
	static const sint32 kHighMemValues[] = { -1, 0, 1, 3, 15, 63, 255 };
	constexpr int kHighMemCount = (int)(sizeof(kHighMemValues) / sizeof(kHighMemValues[0]));
	const bool is65C816 = (sim.GetCPU().GetCPUMode() == kATCPUMode_65C816);
	const bool highMemOverridden = sim.GetHighMemoryBanksOverridden();
	sint32 curHighMem = sim.GetHighMemoryBanks();
	int hmIdx = 0;
	for (int i = 0; i < kHighMemCount; ++i)
		if (kHighMemValues[i] == curHighMem) { hmIdx = i; break; }
	const bool hmDisabled = !is65C816 || highMemOverridden;
	if (hmDisabled) ImGui::BeginDisabled();
	if (ImGui::Combo("High memory banks", &hmIdx, kHighMemLabels, kHighMemCount)) {
		sim.SetHighMemoryBanks(kHighMemValues[hmIdx]);
		sim.ColdReset();
	}
	ImGui::SetItemTooltip("Set the amount of memory available above bank 0 for the 65C816 CPU.");
	if (hmDisabled) ImGui::EndDisabled();

	ImGui::Separator();

	bool preserveExt = sim.IsPreserveExtRAMEnabled();
	if (ImGui::Checkbox("Preserve extended memory on cold reset", &preserveExt))
		sim.SetPreserveExtRAMEnabled(preserveExt);
	ImGui::SetItemTooltip("Keep contents of extended memory intact during cold reset.");
}

// =========================================================================
// Acceleration (matches Windows IDD_CONFIGURE_ACCELERATION)
// =========================================================================

void RenderAccelerationCategory(ATSimulator &sim) {
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
	ImGui::SetItemTooltip(
		"Intercept and accelerate CIO transfers to the host device.\n\n"
		"Requires the Host Device (H:) entry to be added in Configure "
		"System \xe2\x86\x92 Devices \xe2\x86\x92 Add Device \xe2\x86\x92 "
		"HLE Devices \xe2\x86\x92 Host device (H:), with at least one "
		"host path configured.");

	// Shortcut to the H: device's configure dialog if it's installed.
	// The canonical entry point stays Configure System \xe2\x86\x92 Devices,
	// but this saves a round-trip for the common "change a path and go"
	// case.  Disabled (with a hint) when no HostDevice entry exists.
	{
		IATDevice *hostDev = nullptr;
		ATDeviceManager *devMgrH = sim.GetDeviceManager();
		if (devMgrH) {
			for (IATDevice *dev : devMgrH->GetDevices(true, true, true)) {
				ATDeviceInfo info;
				dev->GetDeviceInfo(info);
				if (info.mpDef && info.mpDef->mpTag
					&& !strcmp(info.mpDef->mpTag, "hostfs")) {
					hostDev = dev;
					break;
				}
			}
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(hostDev == nullptr);
		if (ImGui::Button("Configure H:..."))
			ATUIOpenDeviceConfig(hostDev, devMgrH);
		ImGui::EndDisabled();
		if (hostDev == nullptr)
			ImGui::SetItemTooltip(
				"Add Host Device (H:) from Configure System "
				"\xe2\x86\x92 Devices first.");
		else
			ImGui::SetItemTooltip(
				"Open the Host Device (H:) configuration dialog "
				"to set host paths and options.");
	}

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

void RenderSpeedCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Speed control");

	const bool netplayLocksSpeed = ATNetplayGlue::IsActive();

	bool warp = ATUIGetTurbo();
	ImGui::BeginDisabled(netplayLocksSpeed);
	if (ImGui::Checkbox("Run as fast as possible (warp)", &warp))
		ATUISetTurbo(warp);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip(netplayLocksSpeed
		? "Disabled while Playing Online — both peers must run at 1x."
		: "Disable the speed limiter and run the emulation as fast as possible.");

	bool slowmo = ATUIGetSlowMotion();
	ImGui::BeginDisabled(netplayLocksSpeed);
	if (ImGui::Checkbox("Slow Motion", &slowmo))
		ATUISetSlowMotion(slowmo);
	ImGui::EndDisabled();

	ImGui::SeparatorText("Speed adjustment");

	float spd = ATUIGetSpeedModifier();
	static const float kSpdValues[] = { 0.0f, 1.0f, 3.0f, 7.0f };
	static const char *kSpdLabels[] = { "1x (Normal)", "2x", "4x", "8x" };
	int spdIdx = 0;
	for (int i = 0; i < 4; ++i)
		if (kSpdValues[i] == spd) { spdIdx = i; break; }
	ImGui::BeginDisabled(netplayLocksSpeed);
	if (ImGui::Combo("Speed", &spdIdx, kSpdLabels, 4))
		ATUISetSpeedModifier(kSpdValues[spdIdx]);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip(netplayLocksSpeed
		? "Disabled while Playing Online — both peers must run at 1x."
		: "Scale the baseline rate to run the emulation faster or slower.");

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
#ifdef ALTIRRA_NETPLAY_ENABLED
	const bool netplayLocksPause = ATNetplayGlue::IsActive();
#else
	const bool netplayLocksPause = false;
#endif
	if (netplayLocksPause) ImGui::BeginDisabled();
	if (ImGui::Checkbox("Pause when emulator window is inactive", &pauseInactive))
		ATUISetPauseWhenInactive(pauseInactive);
	ImGui::SetItemTooltip(netplayLocksPause
		? "Disabled while Playing Online — stalling the sim would freeze the peer."
		: "Automatically pause the emulation when the emulator window is inactive.");
	if (netplayLocksPause) ImGui::EndDisabled();

	ImGui::SeparatorText("Rewind");

	IATAutoSaveManager &mgr = sim.GetAutoSaveManager();
	bool rewindEnabled = mgr.GetRewindEnabled();
	if (ImGui::Checkbox("Enable automatic rewind recording", &rewindEnabled))
		mgr.SetRewindEnabled(rewindEnabled);
	ImGui::SetItemTooltip("Periodically save emulation state to allow rewinding to a previous point.");
}

// =========================================================================
// Boot (matches Windows IDD_CONFIGURE_BOOT)
// =========================================================================

void RenderBootCategory(ATSimulator &sim) {
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


