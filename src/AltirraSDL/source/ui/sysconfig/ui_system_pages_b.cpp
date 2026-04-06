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

void RenderDevicesCategory(ATSimulator &sim) {
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
								LOG_ERROR("UI", "Failed to add device: %s", entry.tag);
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

void RenderMediaDefaultsCategory(ATSimulator &) {
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

void RenderEnhancedTextCategory(ATSimulator &) {
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

void RenderCaptionCategory(ATSimulator &) {
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

void RenderWorkaroundsCategory(ATSimulator &) {
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

void RenderCompatDBCategory(ATSimulator &) {
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

void RenderErrorHandlingCategory(ATSimulator &) {
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

void RenderFlashCategory(ATSimulator &) {
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
// Accessibility (matches Windows IDD_CONFIGURE_ACCESSIBILITY)
// =========================================================================

void RenderAccessibilityCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;
	ImGui::SeparatorText("Screen reader");
	bool acc = g_ATOptions.mbAccEnabled;
	if (ImGui::Checkbox("Enable screen reader support", &acc)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbAccEnabled = acc;
		if (g_ATOptions != prev) { g_ATOptions.mbDirty = true; ATOptionsRunUpdateCallbacks(&prev); ATOptionsSave(); }
	}
	ImGui::SetItemTooltip("Enable accessibility features for screen reader software.");
}

// =========================================================================
// Debugger settings (matches Windows IDD_CONFIGURE_DEBUGGER)
// =========================================================================

void RenderDebuggerCfgCategory(ATSimulator &sim) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) { ImGui::TextWrapped("Debugger not available."); return; }

	ImGui::SeparatorText("Symbol loading");
	static const char *kSymLoad[] = { "Disabled", "Deferred", "Enabled" };

	int pre = (int)dbg->GetSymbolLoadMode(false);
	if (pre < 0 || pre > 2) pre = 0;
	if (ImGui::Combo("Pre-start", &pre, kSymLoad, 3))
		dbg->SetSymbolLoadMode(false, (ATDebuggerSymbolLoadMode)pre);

	int post = (int)dbg->GetSymbolLoadMode(true);
	if (post < 0 || post > 2) post = 0;
	if (ImGui::Combo("Post-start", &post, kSymLoad, 3))
		dbg->SetSymbolLoadMode(true, (ATDebuggerSymbolLoadMode)post);

	static const char *kScript[] = { "Disabled", "Ask to Load", "Enabled" };
	int sm = (int)dbg->GetScriptAutoLoadMode();
	if (sm < 0 || sm > 2) sm = 0;
	if (ImGui::Combo("Script auto-load", &sm, kScript, 3))
		dbg->SetScriptAutoLoadMode((ATDebuggerScriptAutoLoadMode)sm);

	ImGui::SeparatorText("Options");
	bool autoSys = dbg->IsAutoLoadSystemSymbolsEnabled();
	if (ImGui::Checkbox("Auto-load standard system symbols", &autoSys))
		dbg->SetAutoLoadSystemSymbols(autoSys);

	bool autoKern = sim.IsAutoLoadKernelSymbolsEnabled();
	if (ImGui::Checkbox("Auto-load OS ROM symbols", &autoKern))
		sim.SetAutoLoadKernelSymbolsEnabled(autoKern);

	bool debugLink = dbg->GetDebugLinkEnabled();
	if (ImGui::Checkbox("Enable debug link device", &debugLink))
		dbg->SetDebugLinkEnabled(debugLink);
}

// =========================================================================
// UI (matches Windows IDD_CONFIGURE_UI)
// =========================================================================

void RenderUICategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Window behavior");
	bool pauseMenu = g_ATOptions.mbPauseDuringMenu;
	if (ImGui::Checkbox("Pause when menus are open", &pauseMenu)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbPauseDuringMenu = pauseMenu;
		if (g_ATOptions != prev) { g_ATOptions.mbDirty = true; ATOptionsRunUpdateCallbacks(&prev); ATOptionsSave(); }
	}

	bool single = g_ATOptions.mbSingleInstance;
	if (ImGui::Checkbox("Reuse program instance", &single)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbSingleInstance = single;
		if (g_ATOptions != prev) { g_ATOptions.mbDirty = true; ATOptionsRunUpdateCallbacks(&prev); ATOptionsSave(); }
	}

	ImGui::SeparatorText("Appearance");

	{
		static const char *themeLabels[] = { "Use system setting", "Light", "Dark" };
		int themeIdx = (int)g_ATOptions.mThemeMode;
		if (ImGui::Combo("Theme", &themeIdx, themeLabels, 3)) {
			ATOptions prev(g_ATOptions);
			g_ATOptions.mThemeMode = (ATUIThemeMode)themeIdx;
			if (g_ATOptions != prev) {
				g_ATOptions.mbDirty = true;
				ATOptionsRunUpdateCallbacks(&prev);
				ATOptionsSave();
				ATUIApplyTheme();
			}
		}
	}

	{
		int alphaPct = (int)(g_ATOptions.mUIAlpha * 100.0f + 0.5f);
		if (ImGui::SliderInt("Window opacity (%)", &alphaPct, 20, 100)) {
			ATOptions prev(g_ATOptions);
			g_ATOptions.mUIAlpha = alphaPct / 100.0f;
			if (g_ATOptions != prev) {
				g_ATOptions.mbDirty = true;
				ATOptionsRunUpdateCallbacks(&prev);
				ATOptionsSave();
				ATUIApplyTheme();
			}
		}
	}

	int scale = g_ATOptions.mThemeScale;
	if (scale <= 0) scale = 100;
	if (ImGui::SliderInt("UI scale (%)", &scale, 50, 300)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mThemeScale = scale;
		if (g_ATOptions != prev) { g_ATOptions.mbDirty = true; ATOptionsRunUpdateCallbacks(&prev); ATOptionsSave(); }
	}

	ImGui::SeparatorText("Dialogs");
	if (ImGui::Button("Show all dialogs again"))
		ATUIGenericDialogUndoAllIgnores();
	ImGui::SetItemTooltip("Re-enable all dialogs dismissed with 'Don't ask again'.");
}

// =========================================================================
// Display Effects (matches Windows IDD_CONFIGURE_DISPLAY2, SDL3-adapted)
// =========================================================================

void RenderDisplay2Category(ATSimulator &) {
	extern ATOptions g_ATOptions;
	extern SDL_Window *g_pWindow;

	ImGui::SeparatorText("Full-screen mode");
	bool borderless = g_ATOptions.mbFullScreenBorderless;
	if (ImGui::RadioButton("Borderless windowed", borderless)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbFullScreenBorderless = true;
		g_ATOptions.mFullScreenWidth = 0;
		g_ATOptions.mFullScreenHeight = 0;
		g_ATOptions.mFullScreenRefreshRate = 0;
		if (g_ATOptions != prev) { g_ATOptions.mbDirty = true; ATOptionsRunUpdateCallbacks(&prev); ATOptionsSave(); }
	}
	if (ImGui::RadioButton("Exclusive full-screen", !borderless)) {
		ATOptions prev(g_ATOptions);
		g_ATOptions.mbFullScreenBorderless = false;
		if (g_ATOptions != prev) { g_ATOptions.mbDirty = true; ATOptionsRunUpdateCallbacks(&prev); ATOptionsSave(); }
	}
	if (!borderless) {
		// Enumerate available display modes for the current display
		SDL_DisplayID displayID = g_pWindow
			? SDL_GetDisplayForWindow(g_pWindow)
			: SDL_GetPrimaryDisplay();

		int modeCount = 0;
		SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(displayID, &modeCount);

		// Build preview string for the currently selected mode
		char preview[64];
		if (g_ATOptions.mFullScreenWidth == 0 && g_ATOptions.mFullScreenHeight == 0) {
			snprintf(preview, sizeof(preview), "Desktop resolution");
		} else if (g_ATOptions.mFullScreenRefreshRate > 0) {
			snprintf(preview, sizeof(preview), "%u x %u @ %u Hz",
				g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight,
				g_ATOptions.mFullScreenRefreshRate);
		} else {
			snprintf(preview, sizeof(preview), "%u x %u",
				g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight);
		}

		ImGui::Indent();
		ImGui::SetNextItemWidth(280);
		if (ImGui::BeginCombo("Resolution", preview)) {
			// "Desktop resolution" = exclusive at desktop res (w=0, h=0)
			bool isDesktop = (g_ATOptions.mFullScreenWidth == 0 &&
				g_ATOptions.mFullScreenHeight == 0);
			if (ImGui::Selectable("Desktop resolution", isDesktop)) {
				ATOptions prev(g_ATOptions);
				g_ATOptions.mFullScreenWidth = 0;
				g_ATOptions.mFullScreenHeight = 0;
				g_ATOptions.mFullScreenRefreshRate = 0;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
				}
			}

			// Deduplicated list of available modes (w, h, refresh)
			if (modes) {
				struct ModeEntry { int w, h; float hz; };
				static thread_local std::vector<ModeEntry> uniqueModes;
				uniqueModes.clear();

				for (int i = 0; i < modeCount; i++) {
					const SDL_DisplayMode *m = modes[i];
					float hz = m->refresh_rate;
					bool dup = false;
					for (const auto &u : uniqueModes) {
						if (u.w == m->w && u.h == m->h &&
							(int)(u.hz + 0.5f) == (int)(hz + 0.5f)) {
							dup = true;
							break;
						}
					}
					if (!dup)
						uniqueModes.push_back({m->w, m->h, hz});
				}

				for (const auto &m : uniqueModes) {
					char label[64];
					snprintf(label, sizeof(label), "%d x %d @ %.0f Hz",
						m.w, m.h, m.hz);

					uint32 roundedHz = (uint32)(m.hz + 0.5f);
					bool selected = ((uint32)m.w == g_ATOptions.mFullScreenWidth &&
						(uint32)m.h == g_ATOptions.mFullScreenHeight &&
						roundedHz == g_ATOptions.mFullScreenRefreshRate);

					if (ImGui::Selectable(label, selected)) {
						ATOptions prev(g_ATOptions);
						g_ATOptions.mFullScreenWidth = (uint32)m.w;
						g_ATOptions.mFullScreenHeight = (uint32)m.h;
						g_ATOptions.mFullScreenRefreshRate = roundedHz;
						if (g_ATOptions != prev) {
							g_ATOptions.mbDirty = true;
							ATOptionsRunUpdateCallbacks(&prev);
							ATOptionsSave();
						}
					}
				}
			}

			ImGui::EndCombo();
		}
		ImGui::Unindent();

		if (modes)
			SDL_free(modes);
	}

	ImGui::SeparatorText("Screen effects");
	static char effectPath[512] = {};
	static bool effectInit = false;
	if (!effectInit) {
		const wchar_t *p = g_ATUIManager.GetCustomEffectPath();
		if (p) { VDStringA u8 = VDTextWToU8(VDStringW(p)); strncpy(effectPath, u8.c_str(), sizeof(effectPath)-1); }
		effectInit = true;
	}
	if (ImGui::InputText("Effect file", effectPath, sizeof(effectPath))) {
		VDStringW wp = VDTextU8ToW(VDStringA(effectPath));
		g_ATUIManager.SetCustomEffectPath(wp.c_str(), false);
	}
}

// =========================================================================
// Settings (matches Windows IDD_CONFIGURE_SETTINGS)
// =========================================================================

void RenderSettingsCfgCategory(ATSimulator &) {
	ImGui::SeparatorText("Settings storage");
	bool portable = ATSettingsIsInPortableMode();
	bool migrating = ATSettingsIsMigrationScheduled();
	bool resetting = ATSettingsIsResetPending();

	ImGui::TextWrapped(portable ? "Currently using portable settings (INI file)." : "Currently using standard settings.");
	if (migrating) ImGui::TextColored(ImVec4(1,1,0,1), "Settings migration scheduled for next startup.");
	if (resetting) ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Settings reset scheduled for next startup.");

	ImGui::Spacing();
	bool disabled = resetting || migrating;
	if (disabled) ImGui::BeginDisabled();
	if (ImGui::Button("Reset all settings")) ATSettingsScheduleReset();
	ImGui::SetItemTooltip("Reset all settings to defaults on next startup.");
	if (ImGui::Button(portable ? "Switch to standard settings" : "Switch to portable settings (INI)"))
		ATSettingsScheduleMigration();
	ImGui::SetItemTooltip("Migrate settings on next startup.");
	if (disabled) ImGui::EndDisabled();
}

