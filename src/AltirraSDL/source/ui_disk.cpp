//	AltirraSDL - Disk Drive Manager dialog
//	Mirrors Windows Altirra's Disk Drives dialog (IDD_DISK_DRIVES):
//	per-drive path, write mode, browse/eject/context-menu buttons,
//	Drives 1-8 / 9-15 tabs, emulation level, OK.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <at/atcore/media.h>
#include <at/atcore/vfs.h>
#include <at/atio/diskfs.h>
#include <at/atio/diskfsutil.h>

#include "ui_main.h"
#include "simulator.h"
#include "diskinterface.h"
#include "disk.h"
#include <at/atio/diskimage.h>

extern ATSimulator g_sim;

// =========================================================================
// Create Disk format types (matches Windows ATNewDiskDialog)
// =========================================================================

struct DiskFormatType {
	uint32 sectorSize;
	uint32 sectorCount;
	const char *label;
};

static const DiskFormatType kDiskFormatTypes[] = {
	{ 0,     0, "Custom" },
	{ 128,  720, "Single density (720 sectors, 128 B/sec)" },
	{ 128, 1040, "Medium density (1040 sectors, 128 B/sec)" },
	{ 256,  720, "Double density (720 sectors, 256 B/sec)" },
	{ 256, 1440, "Double-sided DD (1440 sectors, 256 B/sec)" },
	{ 256, 2880, "DSDD 80 tracks (2880 sectors, 256 B/sec)" },
	{ 128, 2002, "8\" single-sided (2002 sectors, 128 B/sec)" },
	{ 128, 4004, "8\" double-sided (4004 sectors, 128 B/sec)" },
	{ 256, 2002, "8\" single-sided DD (2002 sectors, 256 B/sec)" },
	{ 256, 4004, "8\" double-sided DD (4004 sectors, 256 B/sec)" },
};

static constexpr int kNumDiskFormats = (int)(sizeof(kDiskFormatTypes) / sizeof(kDiskFormatTypes[0]));

// Filesystem format labels (matches Windows IDC_FILESYSTEM combo in IDD_CREATE_DISK)
static const char *kFSFormatLabels[] = {
	"None",
	"DOS 2.0S/2.5",
	"DOS 1.0",
	"DOS 3.0",
	"MyDOS",
	"SpartaDOS",
};
static constexpr int kNumFSFormats = 6;

static struct {
	bool show = false;
	int targetDrive = 0;
	int formatIndex = 1;     // default: single density
	int sectorCount = 720;
	int bootSectorCount = 3;
	int sectorSize = 128;    // actual byte value: 128, 256, or 512
	int fsFormatIndex = 0;   // filesystem format (0=None, matches kFSFormatLabels)
} g_createDiskState;

// Check if current geometry supports the selected filesystem
// (matches Windows ATNewDiskDialog::IsCurrentFormatSupported)
static bool IsCreateDiskFormatSupported() {
	int sc = g_createDiskState.sectorCount;
	int ss = g_createDiskState.sectorSize;
	int bs = g_createDiskState.bootSectorCount;

	switch (g_createDiskState.fsFormatIndex) {
		case 0: return true;  // None
		case 1: // DOS 2.0S/2.5
			if (bs != 3) return false;
			if (ss != 128 && ss != 256) return false;
			if (sc == 1040) return ss == 128;
			return sc == 720;
		case 2: // DOS 1.0
			return sc == 720 && ss == 128;
		case 3: // DOS 3.0
			return (sc == 720 || sc == 1040) && ss == 128;
		case 4: // MyDOS
			return sc >= 720 && (ss == 128 || ss == 256);
		case 5: // SpartaDOS
			return sc >= 16;
		default: return false;
	}
}

static void RenderCreateDiskDialog() {
	if (!g_createDiskState.show)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 310), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Create Disk", &g_createDiskState.show, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_createDiskState.show = false;
		ImGui::End();
		return;
	}

	// Format combo
	if (ImGui::Combo("Format", &g_createDiskState.formatIndex,
		[](void *, int idx) -> const char * { return kDiskFormatTypes[idx].label; },
		nullptr, kNumDiskFormats))
	{
		// Update geometry from preset (unless Custom)
		if (g_createDiskState.formatIndex > 0) {
			const auto& fmt = kDiskFormatTypes[g_createDiskState.formatIndex];
			g_createDiskState.sectorCount = (int)fmt.sectorCount;
			g_createDiskState.sectorSize = (int)fmt.sectorSize;
			g_createDiskState.bootSectorCount = 3;
		}
	}

	bool isCustom = (g_createDiskState.formatIndex == 0);

	// Sector count (editable in Custom mode)
	ImGui::BeginDisabled(!isCustom);
	ImGui::InputInt("Sector Count", &g_createDiskState.sectorCount);
	if (g_createDiskState.sectorCount < 1) g_createDiskState.sectorCount = 1;
	if (g_createDiskState.sectorCount > 65535) g_createDiskState.sectorCount = 65535;
	ImGui::EndDisabled();

	// Sector size radio buttons (editable in Custom mode)
	ImGui::BeginDisabled(!isCustom);
	ImGui::Text("Sector Size:");
	ImGui::SameLine();
	int sectorSizeIdx = g_createDiskState.sectorSize == 256 ? 1 :
	                     g_createDiskState.sectorSize == 512 ? 2 : 0;
	if (ImGui::RadioButton("128", sectorSizeIdx == 0)) g_createDiskState.sectorSize = 128;
	ImGui::SameLine();
	if (ImGui::RadioButton("256", sectorSizeIdx == 1)) g_createDiskState.sectorSize = 256;
	ImGui::SameLine();
	if (ImGui::RadioButton("512", sectorSizeIdx == 2)) g_createDiskState.sectorSize = 512;
	ImGui::EndDisabled();

	// Boot sector count
	ImGui::InputInt("Boot Sectors", &g_createDiskState.bootSectorCount);
	if (g_createDiskState.bootSectorCount < 0) g_createDiskState.bootSectorCount = 0;
	if (g_createDiskState.bootSectorCount > 255) g_createDiskState.bootSectorCount = 255;

	// Filesystem format (matches Windows IDC_FILESYSTEM)
	ImGui::Combo("Filesystem", &g_createDiskState.fsFormatIndex, kFSFormatLabels, kNumFSFormats);

	bool formatSupported = IsCreateDiskFormatSupported();
	if (g_createDiskState.fsFormatIndex > 0 && !formatSupported)
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Geometry not supported for this filesystem");

	ImGui::Separator();

	ImGui::BeginDisabled(!formatSupported);
	if (ImGui::Button("Create", ImVec2(80, 0))) {
		int driveIdx = g_createDiskState.targetDrive;
		ATDiskInterface& di = g_sim.GetDiskInterface(driveIdx);
		ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIdx);

		di.UnloadDisk();
		di.CreateDisk(
			(uint32)g_createDiskState.sectorCount,
			(uint32)g_createDiskState.bootSectorCount,
			(uint32)g_createDiskState.sectorSize);

		// Enable drive and set write mode (matches Windows post-creation)
		if (di.GetClientCount() < 2)
			disk.SetEnabled(true);
		di.SetWriteMode(kATMediaWriteMode_VRW);

		// Format with selected filesystem (matches Windows)
		if (g_createDiskState.fsFormatIndex > 0) {
			try {
				IATDiskImage *image = di.GetDiskImage();
				vdautoptr<IATDiskFS> fs;

				switch (g_createDiskState.fsFormatIndex) {
					case 1: fs = ATDiskFormatImageDOS2(image); break;
					case 2: fs = ATDiskFormatImageDOS1(image); break;
					case 3: fs = ATDiskFormatImageDOS3(image); break;
					case 4: fs = ATDiskFormatImageMyDOS(image); break;
					case 5: fs = ATDiskFormatImageSDX2(image); break;
				}

				if (fs)
					fs->Flush();
			} catch (const MyError& e) {
				fprintf(stderr, "[AltirraSDL] Format error: %s\n", e.c_str());
			}
		}

		g_createDiskState.show = false;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0)))
		g_createDiskState.show = false;

	ImGui::End();
}

// =========================================================================
// File dialog callbacks — drive index passed via userdata
// =========================================================================

static void DiskMountCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;

	VDStringW widePath = VDTextU8ToW(filelist[0], -1);
	try {
		g_sim.GetDiskInterface(driveIdx).LoadDisk(widePath.c_str());
		fprintf(stderr, "[AltirraSDL] Mounted D%d: %s\n", driveIdx + 1, filelist[0]);
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Failed to mount D%d: %s\n", driveIdx + 1, filelist[0]);
	}
}

static void DiskSaveAsCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;

	VDStringW widePath = VDTextU8ToW(filelist[0], -1);
	try {
		g_sim.GetDiskInterface(driveIdx).SaveDiskAs(widePath.c_str(), kATDiskImageFormat_ATR);
		fprintf(stderr, "[AltirraSDL] Saved D%d as: %s\n", driveIdx + 1, filelist[0]);
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Failed to save D%d: %s\n", driveIdx + 1, filelist[0]);
	}
}

// Callback for boot sector extraction save dialog
static void BootSectorSaveCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;

	ATDiskInterface& di = g_sim.GetDiskInterface(driveIdx);
	IATDiskImage *image = di.GetDiskImage();
	if (!image) return;

	try {
		uint8 sec[384] = {0};
		for (int i = 0; i < 3; ++i)
			image->ReadPhysicalSector(i, &sec[i * 128], 128);

		VDStringW wpath = VDTextU8ToW(filelist[0], -1);
		VDFile f(wpath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
		f.write(sec, sizeof sec);
	} catch (const MyError& e) {
		fprintf(stderr, "[AltirraSDL] Extract boot sectors failed: %s\n", e.c_str());
	}
}

// Callback for mount folder as virtual disk
struct MountFolderInfo {
	int driveIdx;
	bool sdfs;
};

static void MountFolderCallback(void *userdata, const char * const *filelist, int) {
	MountFolderInfo *info = (MountFolderInfo *)userdata;
	if (!filelist || !filelist[0] || !info) {
		delete info;
		return;
	}

	int driveIdx = info->driveIdx;
	bool sdfs = info->sdfs;
	delete info;

	if (driveIdx < 0 || driveIdx >= 15) return;

	VDStringW widePath = VDTextU8ToW(filelist[0], -1);
	try {
		ATDiskInterface& di = g_sim.GetDiskInterface(driveIdx);
		di.MountFolder(widePath.c_str(), sdfs);

		ATDiskEmulator& disk = g_sim.GetDiskDrive(driveIdx);
		if (di.GetClientCount() < 2)
			disk.SetEnabled(true);

		fprintf(stderr, "[AltirraSDL] Mounted folder on D%d: %s (%s)\n",
			driveIdx + 1, filelist[0], sdfs ? "SDFS" : "DOS2");
	} catch (const MyError& e) {
		fprintf(stderr, "[AltirraSDL] Mount folder failed on D%d: %s\n", driveIdx + 1, e.c_str());
	}
}

static const SDL_DialogFileFilter kDiskFilters[] = {
	{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kDiskSaveFilters[] = {
	{ "ATR Disk Image", "atr" },
	{ "XFD Disk Image", "xfd" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kBootSectorFilters[] = {
	{ "Boot sectors file", "bin" },
	{ "All Files", "*" },
};

// =========================================================================
// Convert to filesystem table (matches Windows IDR_DISK_CONTEXT_MENU)
// =========================================================================

enum ATDiskFormatFileSystem {
	kATDiskFFS_None,
	kATDiskFFS_DOS2,
	kATDiskFFS_DOS1,
	kATDiskFFS_DOS3,
	kATDiskFFS_MyDOS,
	kATDiskFFS_SDFS,
};

struct ConvertEntry {
	const char *label;
	ATDiskFormatFileSystem ffs;
	uint32 sectorSize;
};

static const ConvertEntry kConvertTable[] = {
	{ "DOS 1.0 (SD)",           kATDiskFFS_DOS1, 128 },
	{ "DOS 2.0S/2.5 (SD/ED)",  kATDiskFFS_DOS2, 128 },
	{ "DOS 2.0D (DD)",          kATDiskFFS_DOS2, 256 },
	{ "MyDOS (SD)",             kATDiskFFS_MyDOS, 128 },
	{ "MyDOS (DD)",             kATDiskFFS_MyDOS, 256 },
	{ "SpartaDOS (SD/ED)",      kATDiskFFS_SDFS, 128 },
	{ "SpartaDOS (DD)",         kATDiskFFS_SDFS, 256 },
	{ "SpartaDOS (512)",        kATDiskFFS_SDFS, 512 },
};

static constexpr int kNumConvertFormats = (int)(sizeof(kConvertTable) / sizeof(kConvertTable[0]));

// =========================================================================
// Reinterleave helper (matches Windows ATDiskDriveDialog::Reinterleave)
// =========================================================================

static void Reinterleave(ATDiskInterface& diskIf, ATDiskInterleave interleave) {
	IATDiskImage *img = diskIf.GetDiskImage();
	if (!img)
		return;

	if (!img->IsSafeToReinterleave()) {
		// TODO: confirmation dialog for protected disks
		fprintf(stderr, "[AltirraSDL] Warning: reinterleaving disk that may not work correctly with reordered sectors\n");
	}

	img->Reinterleave(interleave);
	diskIf.OnDiskModified();
}

// =========================================================================
// Convert filesystem helper (matches Windows ATDiskDriveDialog::Convert)
// =========================================================================

static void ConvertFilesystem(ATDiskInterface& diskIf, ATDiskFormatFileSystem ffs, uint32 sectorSize) {
	IATDiskImage *img = diskIf.GetDiskImage();
	if (!img)
		return;

	try {
		vdautoptr<IATDiskFS> fs(ATDiskMountImage(img, true));
		if (!fs)
			throw MyError("The disk image does not use a supported filesystem.");

		vdrefptr<IATDiskImage> newImage;
		vdautoptr<IATDiskFS> newfs;
		uint32 diskSize;

		switch (ffs) {
			case kATDiskFFS_DOS1:
				diskSize = 720;
				ATCreateDiskImage(diskSize, 3, sectorSize, ~newImage);
				newfs = ATDiskFormatImageDOS1(newImage);
				break;

			case kATDiskFFS_DOS2:
				diskSize = ATDiskFSEstimateDOS2SectorsNeeded(*fs, sectorSize);
				ATCreateDiskImage(diskSize, 3, sectorSize, ~newImage);
				newfs = ATDiskFormatImageDOS2(newImage);
				break;

			case kATDiskFFS_MyDOS:
				diskSize = ATDiskFSEstimateMyDOSSectorsNeeded(*fs, sectorSize);
				ATCreateDiskImage(diskSize, 3, sectorSize, ~newImage);
				newfs = ATDiskFormatImageMyDOS(newImage);
				break;

			case kATDiskFFS_SDFS:
				diskSize = ATDiskFSEstimateSDX2SectorsNeeded(*fs, sectorSize);
				ATCreateDiskImage(diskSize, sectorSize >= 512 ? 0 : 3, sectorSize, ~newImage);
				newfs = ATDiskFormatImageSDX2(newImage);
				break;

			default:
				return;
		}

		ATDiskFSCopyTree(*newfs, ATDiskFSKey::None, *fs, ATDiskFSKey::None, true);

		fs = nullptr;
		newfs->Flush();
		newfs = nullptr;

		VDStringW origName { VDFileSplitPath(diskIf.GetPath()) };
		diskIf.LoadDisk(nullptr, origName.c_str(), newImage);
	} catch (const MyError& e) {
		fprintf(stderr, "[AltirraSDL] Convert filesystem failed: %s\n", e.c_str());
	}
}

// =========================================================================
// Expand ARCs helper (matches Windows ID_CONTEXT_EXPANDARCS)
// =========================================================================

static void ExpandARCFiles(ATDiskInterface& diskIf) {
	if (!diskIf.IsDiskLoaded())
		return;

	IATDiskImage *img = diskIf.GetDiskImage();
	if (!img || img->IsDynamic())
		return;

	try {
		if (!(diskIf.GetWriteMode() & kATMediaWriteMode_AllowWrite))
			diskIf.SetWriteMode(kATMediaWriteMode_VRWSafe);

		vdautoptr<IATDiskFS> fs(ATDiskMountImage(img, false));
		if (!fs)
			throw MyError("The disk image does not have a recognized filesystem.");

		fs->SetAllowExtend(true);
		uint32 expanded;

		try {
			expanded = ATDiskRecursivelyExpandARCs(*fs);
		} catch (...) {
			try { fs->Flush(); } catch (...) {}
			throw;
		}

		fs->Flush();
		fprintf(stderr, "[AltirraSDL] Archives expanded: %u\n", expanded);
	} catch (const MyError& e) {
		fprintf(stderr, "[AltirraSDL] Expand ARCs failed: %s\n", e.c_str());
	}

	diskIf.OnDiskChanged(true);
}

// =========================================================================
// Show disk image file in system file manager
// =========================================================================

static void ShowDiskFileInExplorer(ATDiskInterface& diskIf) {
	const wchar_t *path = diskIf.GetPath();
	if (!path) return;

	VDStringW filePath;
	if (!ATVFSExtractFilePath(path, &filePath))
		return;

	VDStringW dirPath = VDFileSplitPathLeft(filePath);
	if (dirPath.empty())
		return;

	VDStringA u8dir = VDTextWToU8(dirPath);
	VDStringA url;
	url.sprintf("file://%s", u8dir.c_str());
	SDL_OpenURL(url.c_str());
}

// =========================================================================
// Emulation mode labels (matches Windows dialog order)
// =========================================================================

static const ATDiskEmulationMode kEmuModeValues[] = {
	kATDiskEmulationMode_Generic,
	kATDiskEmulationMode_Generic57600,
	kATDiskEmulationMode_FastestPossible,
	kATDiskEmulationMode_810,
	kATDiskEmulationMode_1050,
	kATDiskEmulationMode_XF551,
	kATDiskEmulationMode_USDoubler,
	kATDiskEmulationMode_Speedy1050,
	kATDiskEmulationMode_IndusGT,
	kATDiskEmulationMode_Happy810,
	kATDiskEmulationMode_Happy1050,
	kATDiskEmulationMode_1050Turbo,
};
static const char *kEmuModeLabels[] = {
	"Generic",
	"Generic + 57600 baud",
	"Fastest Possible",
	"810",
	"1050",
	"XF551",
	"US Doubler",
	"Speedy 1050",
	"Indus GT",
	"Happy 810",
	"Happy 1050",
	"1050 Turbo",
};
static const int kNumEmuModes = 12;

// Write mode labels (matches Windows: Off/R-O/VRWSafe/VRW/R-W)
static const char *kWriteModeLabels[] = {
	"Off", "R/O", "VRWSafe", "VRW", "R/W"
};
static const ATMediaWriteMode kWriteModeValues[] = {
	kATMediaWriteMode_RO,       // placeholder for Off
	kATMediaWriteMode_RO,
	kATMediaWriteMode_VRWSafe,
	kATMediaWriteMode_VRW,
	kATMediaWriteMode_RW,
};

static int GetWriteModeIndex(ATDiskInterface& di) {
	if (!di.IsDiskLoaded()) return 0;
	switch (di.GetWriteMode()) {
	case kATMediaWriteMode_RO:       return 1;
	case kATMediaWriteMode_VRWSafe:  return 2;
	case kATMediaWriteMode_VRW:      return 3;
	case kATMediaWriteMode_RW:       return 4;
	default:                         return 1;
	}
}

// =========================================================================
// Context menu for per-drive "+" button
// Matches Windows IDR_DISK_CONTEXT_MENU (uidisk.cpp lines 1128-1456)
// =========================================================================

static void RenderDiskDriveContextMenu(int driveIdx, ATDiskInterface& di,
	ATSimulator& sim, ATUIState& state, SDL_Window *window)
{
	if (!ImGui::BeginPopup("##DriveCtx"))
		return;

	const bool haveDisk = di.IsDiskLoaded();
	const bool haveNonDynamicDisk = haveDisk && !di.GetDiskImage()->IsDynamic();
	const bool haveBackedDisk = haveDisk && di.IsDiskBacked();
	bool canShowFile = false;
	if (haveBackedDisk) {
		const wchar_t *path = di.GetPath();
		if (path)
			canShowFile = ATVFSExtractFilePath(path, nullptr);
	}

	// --- Disk operations (matches Windows RC layout) ---
	if (ImGui::MenuItem("New disk...", nullptr, false, true)) {
		g_createDiskState.show = true;
		g_createDiskState.targetDrive = driveIdx;
	}

	// Save Disk: enabled when non-dynamic disk loaded (matches Windows).
	// If dirty and updatable, saves in place. If dirty but not updatable,
	// falls through to Save As (matches Windows [[fallthrough]] behavior).
	if (ImGui::MenuItem("Save disk", nullptr, false, haveNonDynamicDisk)) {
		if (di.IsDirty()) {
			if (di.GetDiskImage()->IsUpdatable()) {
				try {
					const auto writeMode = di.GetWriteMode();
					if ((writeMode & kATMediaWriteMode_AllowWrite) && !(writeMode & kATMediaWriteMode_AutoFlush))
						di.SetWriteMode(kATMediaWriteMode_RW);
					di.SaveDisk();
				} catch (const MyError& e) {
					fprintf(stderr, "[AltirraSDL] Save disk failed: %s\n", e.c_str());
				}
			} else {
				// Not updatable — fall through to Save As (matches Windows)
				SDL_ShowSaveFileDialog(DiskSaveAsCallback,
					(void *)(intptr_t)driveIdx, window,
					kDiskSaveFilters, 3, nullptr);
			}
		}
	}

	if (ImGui::MenuItem("Save disk as...", nullptr, false, haveNonDynamicDisk)) {
		SDL_ShowSaveFileDialog(DiskSaveAsCallback,
			(void *)(intptr_t)driveIdx, window,
			kDiskSaveFilters, 3, nullptr);
	}

	if (ImGui::MenuItem("Explore disk...", nullptr, false, haveDisk)) {
		const auto writeMode = di.GetWriteMode();
		bool writable = (writeMode & kATMediaWriteMode_AllowWrite) != 0;
		bool autoFlush = (writeMode & kATMediaWriteMode_AutoFlush) != 0;
		ATUIOpenDiskExplorerForDrive(driveIdx, writable, autoFlush);
		state.showDiskExplorer = true;
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Revert disk", nullptr, false, haveDisk && di.CanRevert()))
		di.RevertDisk();

	if (ImGui::MenuItem("Show disk image file", nullptr, false, canShowFile))
		ShowDiskFileInExplorer(di);

	ImGui::Separator();

	// --- Drive operations (submenus replace Windows two-step selection) ---
	if (ImGui::BeginMenu("Swap with another drive")) {
		for (int target = 0; target < 15; ++target) {
			if (target == driveIdx) continue;
			char label[16];
			snprintf(label, sizeof(label), "D%d:", target + 1);
			if (ImGui::MenuItem(label))
				sim.SwapDrives(driveIdx, target);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Shift to another drive")) {
		for (int target = 0; target < 15; ++target) {
			if (target == driveIdx) continue;
			char label[16];
			snprintf(label, sizeof(label), "D%d:", target + 1);
			if (ImGui::MenuItem(label)) {
				int direction = target < driveIdx ? -1 : +1;
				for (int j = driveIdx; j != target; j += direction)
					sim.SwapDrives(j, j + direction);
			}
		}
		ImGui::EndMenu();
	}

	// --- Change Interleave submenu (with group separators matching Windows RC) ---
	if (ImGui::BeginMenu("Change interleave", haveNonDynamicDisk)) {
		if (ImGui::MenuItem("Default"))
			Reinterleave(di, kATDiskInterleave_Default);
		if (ImGui::MenuItem("1:1"))
			Reinterleave(di, kATDiskInterleave_1_1);

		ImGui::Separator();  // SD group
		if (ImGui::MenuItem("12:1 (810 rev. B SD)"))
			Reinterleave(di, kATDiskInterleave_SD_12_1);
		if (ImGui::MenuItem("9:1 (SD)"))
			Reinterleave(di, kATDiskInterleave_SD_9_1);
		if (ImGui::MenuItem("9:1 (SD improved)"))
			Reinterleave(di, kATDiskInterleave_SD_9_1_REV);
		if (ImGui::MenuItem("5:1 (US Doubler SD fast)"))
			Reinterleave(di, kATDiskInterleave_SD_5_1);
		if (ImGui::MenuItem("4:1 (Indus GT SuperSynchromesh)"))
			Reinterleave(di, kATDiskInterleave_SD_4_1);
		if (ImGui::MenuItem("2:1 (SD)"))
			Reinterleave(di, kATDiskInterleave_SD_2_1);

		ImGui::Separator();  // ED group
		if (ImGui::MenuItem("13:1 (ED)"))
			Reinterleave(di, kATDiskInterleave_ED_13_1);
		if (ImGui::MenuItem("12:1 (ED improved)"))
			Reinterleave(di, kATDiskInterleave_ED_12_1);

		ImGui::Separator();  // DD group
		if (ImGui::MenuItem("16:1 (815 DD)"))
			Reinterleave(di, kATDiskInterleave_DD_16_1);
		if (ImGui::MenuItem("15:1 (XF551 DD)"))
			Reinterleave(di, kATDiskInterleave_DD_15_1);
		if (ImGui::MenuItem("9:1 (XF551 DD fast)"))
			Reinterleave(di, kATDiskInterleave_DD_9_1);
		if (ImGui::MenuItem("7:1 (US Doubler DD fast)"))
			Reinterleave(di, kATDiskInterleave_DD_7_1);

		ImGui::EndMenu();
	}

	// --- Convert To Filesystem submenu ---
	if (ImGui::BeginMenu("Convert filesystem", haveNonDynamicDisk)) {
		for (int i = 0; i < kNumConvertFormats; ++i) {
			if (ImGui::MenuItem(kConvertTable[i].label))
				ConvertFilesystem(di, kConvertTable[i].ffs, kConvertTable[i].sectorSize);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// --- Virtual disk operations ---
	// MountFolder() internally calls UnloadDisk(), so no pre-eject needed.
	// The folder dialog is async — don't eject until the user actually picks a folder.
	if (ImGui::MenuItem("Mount folder as virtual DOS 2 disk...")) {
		auto *info = new MountFolderInfo{driveIdx, false};
		SDL_ShowOpenFolderDialog(MountFolderCallback, info, window, nullptr, false);
	}

	if (ImGui::MenuItem("Mount folder as virtual SpartaDOS disk...")) {
		auto *info = new MountFolderInfo{driveIdx, true};
		SDL_ShowOpenFolderDialog(MountFolderCallback, info, window, nullptr, false);
	}

	if (ImGui::MenuItem("Extract boot sectors for virtual DOS 2 disk...", nullptr, false, haveDisk)) {
		IATDiskImage *image = di.GetDiskImage();
		if (image && image->GetBootSectorCount() == 3) {
			SDL_ShowSaveFileDialog(BootSectorSaveCallback,
				(void *)(intptr_t)driveIdx, window,
				kBootSectorFilters, 2, nullptr);
		} else {
			fprintf(stderr, "[AltirraSDL] Disk does not have standard DOS boot sectors.\n");
		}
	}

	if (ImGui::MenuItem("Recursively expand all .ARChive files...", nullptr, false, haveNonDynamicDisk))
		ExpandARCFiles(di);

	ImGui::EndPopup();
}

// =========================================================================
// Render
// =========================================================================

void ATUIRenderDiskManager(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	ImGui::SetNextWindowSize(ImVec2(720, 460), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Disk drives", &state.showDiskManager, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showDiskManager = false;
		ImGui::End();
		return;
	}

	// --- Drives 1-8 / 9-15 tab selector (matches Windows radio buttons) ---
	static int driveTab = 0;  // 0 = drives 1-8, 1 = drives 9-15
	if (ImGui::RadioButton("Drives 1-8", driveTab == 0)) driveTab = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("Drives 9-15", driveTab == 1)) driveTab = 1;

	int baseIdx = driveTab * 8;
	int numDrives = (driveTab == 0) ? 8 : 7;  // Drives 9-15 = 7 drives

	// --- Per-drive table ---
	if (ImGui::BeginTable("##Drives", 6,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Drive", ImGuiTableColumnFlags_WidthFixed, 48);
		ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Write Mode", ImGuiTableColumnFlags_WidthFixed, 100);
		ImGui::TableSetupColumn("##mount", ImGuiTableColumnFlags_WidthFixed, 32);
		ImGui::TableSetupColumn("##eject", ImGuiTableColumnFlags_WidthFixed, 48);
		ImGui::TableSetupColumn("##more", ImGuiTableColumnFlags_WidthFixed, 32);
		ImGui::TableHeadersRow();

		for (int i = 0; i < numDrives; ++i) {
			int driveIdx = baseIdx + i;
			ImGui::PushID(driveIdx);
			ImGui::TableNextRow();

			ATDiskInterface& di = sim.GetDiskInterface(driveIdx);
			bool loaded = di.IsDiskLoaded();
			bool dirty = loaded && di.IsDirty();

			// Drive label
			ImGui::TableNextColumn();
			if (dirty)
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "D%d:", driveIdx + 1);
			else
				ImGui::Text("D%d:", driveIdx + 1);

			// Image name
			ImGui::TableNextColumn();
			if (loaded) {
				const wchar_t *path = di.GetPath();
				if (path && *path) {
					VDStringA u8 = VDTextWToU8(VDStringW(path));
					const char *base = u8.c_str();
					const char *p = strrchr(base, '/');
					if (p) base = p + 1;
					ImGui::TextUnformatted(base);
				} else {
					ImGui::TextDisabled("(loaded)");
				}
			} else {
				ImGui::TextDisabled("(empty)");
			}

			// Write mode combo (matches Windows per-drive IDC_WRITEMODE)
			ImGui::TableNextColumn();
			int wmIdx = GetWriteModeIndex(di);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::Combo("##wm", &wmIdx, kWriteModeLabels, 5)) {
				if (wmIdx == 0) {
					// "Off" = eject
					if (loaded) di.UnloadDisk();
				} else if (loaded) {
					di.SetWriteMode(kWriteModeValues[wmIdx]);
				}
			}

			// Browse button (matches Windows IDC_BROWSE)
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("...")) {
				SDL_ShowOpenFileDialog(DiskMountCallback,
					(void *)(intptr_t)driveIdx, window,
					kDiskFilters, 2, nullptr, false);
			}

			// Eject button (matches Windows IDC_EJECT)
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Eject") && loaded)
				di.UnloadDisk();

			// More button (context menu — matches Windows IDC_MORE / "+")
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("+")) {
				ImGui::OpenPopup("##DriveCtx");
			}

			RenderDiskDriveContextMenu(driveIdx, di, sim, state, window);

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::Separator();

	// --- Emulation mode (global for all drives — matches Windows IDC_EMULATION_LEVEL) ---
	ATDiskEmulationMode curEmu = sim.GetDiskDrive(0).GetEmulationMode();
	int emuIdx = 0;
	for (int i = 0; i < kNumEmuModes; ++i)
		if (kEmuModeValues[i] == curEmu) { emuIdx = i; break; }

	if (ImGui::Combo("Emulation level", &emuIdx, kEmuModeLabels, kNumEmuModes)) {
		for (int i = 0; i < 15; ++i)
			sim.GetDiskDrive(i).SetEmulationMode(kEmuModeValues[emuIdx]);
	}

	ImGui::Separator();

	// OK button (matches Windows DEFPUSHBUTTON "OK")
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showDiskManager = false;

	ImGui::End();

	// Create Disk sub-dialog (opened from context menu)
	RenderCreateDiskDialog();
}
