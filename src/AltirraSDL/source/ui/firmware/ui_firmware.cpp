//	AltirraSDL - Firmware Manager and firmware category page
//	Firmware add/edit/audit/scan dialog and the firmware settings category
//	within the System Configuration dialog.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "ui_file_dialog_sdl3.h"
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/filesys.h>
#include <vd2/system/unknown.h>
#include <vd2/system/file.h>
#include <vd2/system/zip.h>

#include "ui_main.h"
#include "ui_firmware_internal.h"
#include "simulator.h"
#include "firmwaremanager.h"
#include "firmwaredetect.h"
#include "uiaccessors.h"
#include "uiconfirm.h"
#include <at/atcore/progress.h>

#include <mutex>
#include <thread>
#include <unordered_map>

extern ATSimulator g_sim;

// Firmware Manager window visibility (used by drag-and-drop handler in main loop)
bool g_showFirmwareManager = false;

// =========================================================================
// Firmware Manager dialog
// (matches Windows IDD_FIRMWARE from uifirmware.cpp)
// =========================================================================

// Firmware type display name table (mirrors Windows ATUIFirmwareCategoryEntry)
struct FwTypeNameEntry {
	ATFirmwareType type;
	const char *shortName;
	const char *longName; // nullptr = use shortName
};

struct FwCategoryEntry {
	const char *categoryName;
	const FwTypeNameEntry *entries;
	int count;
};

static const FwTypeNameEntry kFwTypes_Computer[] = {
	{ kATFirmwareType_Kernel800_OSA, "400/800 OS-A" },
	{ kATFirmwareType_Kernel800_OSB, "400/800 OS-B" },
	{ kATFirmwareType_Kernel1200XL, "1200XL OS" },
	{ kATFirmwareType_KernelXL, "XL/XE OS" },
	{ kATFirmwareType_KernelXEGS, "XEGS OS" },
	{ kATFirmwareType_Game, "XEGS Game" },
	{ kATFirmwareType_1400XLHandler, "1400XL Handler", "1400XL/XLD Handler" },
	{ kATFirmwareType_1450XLDiskHandler, "1450XLD Disk Handler" },
	{ kATFirmwareType_1450XLDiskController, "1450XLD Disk Controller" },
	{ kATFirmwareType_1450XLTONGDiskController, "\"TONG\" Disk Controller", "1450XLD \"TONG\" Disk Controller" },
	{ kATFirmwareType_Kernel5200, "5200 OS" },
	{ kATFirmwareType_Basic, "BASIC", "Internal BASIC (XL/XE/XEGS)" },
};

static const FwTypeNameEntry kFwTypes_Printers[] = {
	{ kATFirmwareType_820, "820", "820 40-Column Printer Firmware" },
	{ kATFirmwareType_1025, "1025", "1025 80-Column Printer Firmware" },
	{ kATFirmwareType_1029, "1029", "1029 80-Column Printer Firmware" },
};

static const FwTypeNameEntry kFwTypes_DiskDrives[] = {
	{ kATFirmwareType_810, "810", "810 Disk Drive Firmware" },
	{ kATFirmwareType_Happy810, "Happy 810", "Happy 810 Disk Drive Firmware" },
	{ kATFirmwareType_810Archiver, "810 Archiver", "810 Archiver Disk Drive Firmware" },
	{ kATFirmwareType_810Turbo, "810 Turbo", "810 Turbo Disk Drive Firmware" },
	{ kATFirmwareType_815, "815", "815 Disk Drive Firmware" },
	{ kATFirmwareType_1050, "1050", "1050 Disk Drive Firmware" },
	{ kATFirmwareType_1050Duplicator, "1050 Duplicator", "1050 Duplicator Disk Drive Firmware" },
	{ kATFirmwareType_USDoubler, "US Doubler", "US Doubler Disk Drive Firmware" },
	{ kATFirmwareType_Speedy1050, "Speedy 1050", "Speedy 1050 Disk Drive Firmware" },
	{ kATFirmwareType_Happy1050, "Happy 1050", "Happy 1050 Disk Drive Firmware" },
	{ kATFirmwareType_SuperArchiver, "Super Archiver", "Super Archiver Disk Drive Firmware" },
	{ kATFirmwareType_TOMS1050, "TOMS 1050", "TOMS 1050 Disk Drive Firmware" },
	{ kATFirmwareType_Tygrys1050, "Tygrys 1050", "Tygrys 1050 Disk Drive Firmware" },
	{ kATFirmwareType_IndusGT, "Indus GT", "Indus GT Disk Drive Firmware" },
	{ kATFirmwareType_1050Turbo, "1050 Turbo", "1050 Turbo Disk Drive Firmware" },
	{ kATFirmwareType_1050TurboII, "1050 Turbo II", "1050 Turbo II Disk Drive Firmware" },
	{ kATFirmwareType_ISPlate, "I.S. Plate", "I.S. Plate Disk Drive Firmware" },
	{ kATFirmwareType_XF551, "XF551", "XF551 Disk Drive Firmware" },
	{ kATFirmwareType_ATR8000, "ATR8000", "ATR8000 Disk Drive Firmware" },
	{ kATFirmwareType_Percom, "PERCOM RFD", "PERCOM RFD Disk Drive Firmware" },
	{ kATFirmwareType_PercomAT, "PERCOM AT-88", "PERCOM AT-88 Disk Drive Firmware" },
	{ kATFirmwareType_PercomATSPD, "PERCOM AT88-SPD", "PERCOM AT88-SPD Disk Drive Firmware" },
	{ kATFirmwareType_AMDC, "Amdek AMDC", "Amdek AMDC-I/II Disk Drive Firmware" },
	{ kATFirmwareType_SpeedyXF, "Speedy XF", "Speedy XF Disk Drive Firmware" },
};

static const FwTypeNameEntry kFwTypes_Hardware[] = {
	{ kATFirmwareType_U1MB, "Ultimate1MB" },
	{ kATFirmwareType_MyIDE2, "MyIDE-II" },
	{ kATFirmwareType_SIDE, "SIDE" },
	{ kATFirmwareType_SIDE2, "SIDE 2" },
	{ kATFirmwareType_SIDE3, "SIDE 3" },
	{ kATFirmwareType_KMKJZIDE, "KMK/JZ IDE" },
	{ kATFirmwareType_KMKJZIDE2, "KMK/JZ IDE 2 Main", "KMK/JZ IDE 2 (IDEPlus) main" },
	{ kATFirmwareType_KMKJZIDE2_SDX, "KMK/JZ IDE 2 SDX", "KMK/JZ IDE 2 (IDEPlus) SDX" },
	{ kATFirmwareType_BlackBox, "Black Box" },
	{ kATFirmwareType_BlackBoxFloppy, "BB Floppy Board", "Black Box Floppy Board" },
	{ kATFirmwareType_MIO, "MIO" },
	{ kATFirmwareType_835, "835", "835 Modem Internal ROM Firmware" },
	{ kATFirmwareType_850, "850", "850 Interface Module" },
	{ kATFirmwareType_1030Firmware, "1030 Download", "1030 Modem Download Image" },
	{ kATFirmwareType_1030InternalROM, "1030 Internal", "1030 Modem Internal ROM Firmware" },
	{ kATFirmwareType_1030ExternalROM, "1030 External", "1030 Modem External ROM Firmware" },
	{ kATFirmwareType_RapidusFlash, "Rapidus Flash", "Rapidus Flash Firmware" },
	{ kATFirmwareType_RapidusCorePBI, "Rapidus Core", "Rapidus Core PBI Firmware" },
	{ kATFirmwareType_WarpOS, "APE Warp+", "APE Warp+ OS 32-in-1 Firmware" },
	{ kATFirmwareType_1090Firmware, "1090 Firmware", "1090 80-Column Video Card Firmware" },
	{ kATFirmwareType_1090Charset, "1090 Charset", "1090 80-Column Video Card Charset" },
	{ kATFirmwareType_Bit3Firmware, "Bit3 Firmware", "Bit 3 Full-View 80 Firmware" },
	{ kATFirmwareType_Bit3Charset, "Bit3 Charset", "Bit 3 Full-View 80 Charset" },
	{ kATFirmwareType_KarinMaxiDrive, "Karin Maxi Drive", "Karin Maxi Drive Firmware" },
};

static const FwCategoryEntry kFwCategories[] = {
	{ "Computer", kFwTypes_Computer, (int)(sizeof(kFwTypes_Computer)/sizeof(kFwTypes_Computer[0])) },
	{ "Printers", kFwTypes_Printers, (int)(sizeof(kFwTypes_Printers)/sizeof(kFwTypes_Printers[0])) },
	{ "Disk Drives", kFwTypes_DiskDrives, (int)(sizeof(kFwTypes_DiskDrives)/sizeof(kFwTypes_DiskDrives[0])) },
	{ "Hardware", kFwTypes_Hardware, (int)(sizeof(kFwTypes_Hardware)/sizeof(kFwTypes_Hardware[0])) },
};

static const char *GetFirmwareTypeName(ATFirmwareType type) {
	for (const auto &cat : kFwCategories)
		for (int i = 0; i < cat.count; ++i)
			if (cat.entries[i].type == type)
				return cat.entries[i].shortName;
	return "(Unknown)";
}

// Specific firmware type labels (matches Windows GetSpecificFirmwareLabel)
static const char *GetSpecificFirmwareLabel(ATSpecificFirmwareType ft) {
	switch (ft) {
		case kATSpecificFirmwareType_BASICRevA: return "BASIC rev. A";
		case kATSpecificFirmwareType_BASICRevB: return "BASIC rev. B";
		case kATSpecificFirmwareType_BASICRevC: return "BASIC rev. C";
		case kATSpecificFirmwareType_OSA:       return "OS-A";
		case kATSpecificFirmwareType_OSB:       return "OS-B";
		case kATSpecificFirmwareType_XLOSr2:    return "XL/XE OS ver. 2";
		case kATSpecificFirmwareType_XLOSr4:    return "XL/XE/XEGS OS ver. 4";
		default: return nullptr;
	}
}

// Compute specific firmware bitmask for a firmware entry
static uint32 GetSpecificMask(ATFirmwareManager *fwm, uint64 id) {
	uint32 mask = 0;
	for (uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
		if (id && fwm->GetSpecificFirmware((ATSpecificFirmwareType)i) == id)
			mask |= (1 << i);
	}
	return mask;
}

// Build "Use for" string from specific mask
static VDStringA BuildUseForString(uint32 specificMask) {
	VDStringA s;
	for (uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
		if (specificMask & (1 << i)) {
			if (!s.empty()) s += ", ";
			const char *label = GetSpecificFirmwareLabel((ATSpecificFirmwareType)i);
			if (label) s += label;
		}
	}
	return s;
}

// Firmware Manager state
static struct {
	bool editOpen = false;
	int editIdx = -1;
	uint64 editId = 0;
	char editName[256] = {};
	int editTypeCombo = -1; // -1 = "(Unknown)" placeholder; OK is rejected until user picks a real type
	ATFirmwareType editType = kATFirmwareType_Unknown;
	bool editOptionInvert = false; // OPTION key inverted flag (XL/XEGS kernels)

	// When true, the edit dialog is for a newly-imported firmware that has
	// NOT yet been committed to the firmware manager.  Matches Windows
	// ATUIDialogFirmware::Add() flow, which only calls AddFirmware() after
	// the user confirms the edit dialog; clicking Cancel discards the new
	// entry entirely.
	bool editAddMode = false;
	ATFirmwareInfo pendingNewInfo;
	ATSpecificFirmwareType pendingNewSpecific = kATSpecificFirmwareType_None;
	vdfastvector<uint8> pendingNewData; // used for CRC display before commit

	int selectedIdx = -1;
	ATFirmwareType filterType = kATFirmwareType_Unknown; // Unknown = show all

	// Audit state
	bool auditOpen = false;

	// Scan result feedback (matches Windows result dialog)
	bool scanResultPending = false;
	int scanResultNew = 0;
	int scanResultDuplicate = 0;

	// Blank firmware warning state: 0=idle, 1=need to open popup, 2=popup open (waiting for user)
	int blankWarningState = 0;

	// Inline rename (matches Windows VDUIProxyListView OnItemLabelChanging/
	// Changed flow: F2 on a custom entry swaps the Selectable for an
	// InputText; Enter commits, Esc cancels).
	int renameIdx = -1;
	char renameBuf[256] = {};
	bool renameFocusRequested = false;

	// Drop/Add error popup (matches Windows ATUIDialogFirmware::OnDropFiles
	// which calls ShowError on MyError).  The message is populated by the
	// dialog/drop callback thread and consumed on the main thread.
	bool addErrorPending = false;
	VDStringA addErrorMsg;
} g_fwMgr;

// Audit dialog data — background thread scans firmware CRCs incrementally
// (matches Windows ATUIDialogKnownFirmwareAudit which uses VDThread)
struct AuditEntry {
	VDStringA typeName;
	VDStringA description;
	uint32 size;
	uint32 crc;
	VDStringA matchedImage; // empty = not found in user's collection
};

// Audit background scan state
// Named struct (not anonymous) so that nested types like ScanItem have
// proper linkage — MSVC can fail to instantiate vdvector<T> templates
// when T is defined inside an unnamed struct.
struct AuditState {
	std::mutex mutex;
	std::thread thread;
	bool cancelRequested = false;
	bool scanComplete = false;

	// Written by main thread before starting scan, read by scan thread
	struct ScanItem {
		VDStringW path;
		uint64 fwId;
		VDStringA name;
	};
	vdvector<ScanItem> scanItems;

	// Written by scan thread under mutex, read by main thread
	size_t itemsScanned = 0;
	// Map: known firmware index -> matching user firmware name (populated by scan thread)
	std::unordered_map<size_t, VDStringA> knownToUserMatch;

	// Built by main thread from known firmware DB (immutable during scan)
	vdvector<AuditEntry> entries;
	// Map: known firmware index -> entries[] index
	std::unordered_map<size_t, size_t> knownIdxToEntryIdx;
};
static AuditState g_audit;

// Build flat type list for combo (all types across all categories) — cached
struct FwTypeComboEntry { ATFirmwareType type; const char *name; };
static const std::vector<FwTypeComboEntry>& BuildFwTypeCombo() {
	static std::vector<FwTypeComboEntry> v;
	if (v.empty()) {
		for (const auto &cat : kFwCategories)
			for (int i = 0; i < cat.count; ++i)
				v.push_back({ cat.entries[i].type, cat.entries[i].shortName });
	}
	return v;
}

// Pending firmware add — populated by file dialog callback thread, consumed on main thread
static std::mutex g_fwAddMutex;
static struct {
	bool pending = false;
	VDStringW path;
	vdfastvector<uint8> data;
	ATFirmwareInfo info;
	ATSpecificFirmwareType specificType = kATSpecificFirmwareType_None;
	ATFirmwareDetection detection = ATFirmwareDetection::None;
	bool isBlank = false; // true if ROM is all-same-byte (matches Windows blank firmware check)
} g_fwPendingAdd;

// Open the Edit Firmware Settings dialog for an already-committed
// firmware entry.  Shared between the context-menu "Edit..." item and
// double-click handler so both stay in sync.
static void OpenEditFirmwareDialog(ATFirmwareManager *fwm, uint64 id) {
	ATFirmwareInfo info;
	if (!fwm->GetFirmwareInfo(id, info))
		return;

	// Defensive: make sure we're not inheriting stale add-mode state
	// from a previous import that never ran through the OK/Cancel path
	// (e.g., the firmware manager was closed with the dialog still open).
	g_fwMgr.editAddMode = false;
	g_fwMgr.pendingNewInfo = ATFirmwareInfo{};
	g_fwMgr.pendingNewSpecific = kATSpecificFirmwareType_None;
	g_fwMgr.pendingNewData.clear();

	g_fwMgr.editOpen = true;
	g_fwMgr.editId = id;
	g_fwMgr.editType = info.mType;
	g_fwMgr.editOptionInvert = (info.mFlags & 1) != 0;
	snprintf(g_fwMgr.editName, sizeof(g_fwMgr.editName), "%s",
		VDTextWToU8(info.mName).c_str());

	// Match the Add path: leave combo at -1 (="(Unknown)") when the
	// firmware's recorded type is Unknown, so the OK button stays
	// disabled until the user actually picks a type rather than
	// silently defaulting to the first entry (Kernel800_OSA).
	const auto& types = BuildFwTypeCombo();
	g_fwMgr.editTypeCombo = -1;
	if (info.mType != kATFirmwareType_Unknown) {
		for (int j = 0; j < (int)types.size(); ++j) {
			if (types[j].type == info.mType) {
				g_fwMgr.editTypeCombo = j;
				break;
			}
		}
	}
}

// File dialog callback for Add Firmware — runs on SDL dialog thread
static void FirmwareAddCallback(void *userdata, const char * const *filelist, int filter) {
	(void)userdata;
	(void)filter;
	if (!filelist || !filelist[0])
		return;

	// Reject if a previous add is still pending (e.g., blank firmware warning showing)
	{
		std::lock_guard<std::mutex> lock(g_fwAddMutex);
		if (g_fwPendingAdd.pending)
			return;
	}

	VDStringW wpath = VDTextU8ToW(VDStringA(filelist[0]));

	// Read file on callback thread (file I/O is thread-safe)
	vdfastvector<uint8> data;
	VDStringA errMsg;
	try {
		VDFile f(wpath.c_str());
		sint64 size = f.size();
		// Match Windows uifirmware.cpp: no hard size cap on import.  The
		// firmware manager only needs the file data to run autodetect
		// and the blank-file check; SIDE 3 ships an 8 MB ROM and future
		// devices may be larger still.  Cap at 64 MB to stop obvious
		// garbage (and keep the blank-byte scan bounded).
		if (size <= 0) {
			errMsg.sprintf("Firmware file \"%s\" is empty.",
				VDTextWToU8(VDStringW(VDFileSplitPath(wpath.c_str()))).c_str());
		} else if (size > 64 * 1024 * 1024) {
			errMsg.sprintf("Firmware file \"%s\" is too large (%lld bytes).  "
				"Maximum supported size is 64 MB.",
				VDTextWToU8(VDStringW(VDFileSplitPath(wpath.c_str()))).c_str(),
				(long long)size);
		} else {
			data.resize((size_t)size);
			f.read(data.data(), (long)size);
			f.close();
		}
	} catch (const MyError& e) {
		errMsg = e.c_str();
	} catch (...) {
		errMsg = "Unknown error reading firmware file.";
	}

	if (!errMsg.empty()) {
		// Surface the failure via the error popup on the main thread
		// (matches Windows OnDropFiles ShowError path).
		std::lock_guard<std::mutex> lock(g_fwAddMutex);
		g_fwMgr.addErrorPending = true;
		g_fwMgr.addErrorMsg = std::move(errMsg);
		return;
	}

	// Auto-detect firmware type (pure computation, thread-safe)
	ATFirmwareInfo info;
	ATSpecificFirmwareType specificType = kATSpecificFirmwareType_None;
	sint32 knownIdx = -1;
	ATFirmwareDetection det = ATFirmwareAutodetect(data.data(), (uint32)data.size(), info, specificType, knownIdx);

	// Build firmware info
	ATFirmwareInfo newFw;
	newFw.mId = ATGetFirmwareIdFromPath(wpath.c_str());
	newFw.mPath = wpath;
	newFw.mFlags = 0;
	newFw.mbVisible = true;
	newFw.mbAutoselect = false;

	if (det != ATFirmwareDetection::None) {
		newFw.mType = info.mType;
		if (!info.mName.empty())
			newFw.mName = info.mName;
		else
			newFw.mName = VDFileSplitExtLeft(VDStringW(VDFileSplitPath(wpath.c_str())));
	} else {
		newFw.mType = kATFirmwareType_Unknown;
		newFw.mName = VDFileSplitExtLeft(VDStringW(VDFileSplitPath(wpath.c_str())));
	}

	// Check if ROM is blank (all same byte) — matches Windows blank firmware check
	bool isBlank = false;
	if (!data.empty()) {
		uint8 first = data[0];
		isBlank = true;
		for (size_t i = 1; i < data.size(); ++i) {
			if (data[i] != first) { isBlank = false; break; }
		}
	}

	// Hand off to main thread
	std::lock_guard<std::mutex> lock(g_fwAddMutex);
	g_fwPendingAdd.pending = true;
	g_fwPendingAdd.path = wpath;
	g_fwPendingAdd.data = std::move(data);
	g_fwPendingAdd.isBlank = isBlank;
	g_fwPendingAdd.info = std::move(newFw);
	g_fwPendingAdd.specificType = specificType;
	g_fwPendingAdd.detection = det;
}

// Process pending firmware add on main thread (called each frame from RenderFirmwareManager)
static void ProcessPendingFirmwareAdd(ATFirmwareManager *fwm) {
	// Check for blank firmware warning first — if pending or active, don't process yet
	if (g_fwMgr.blankWarningState != 0)
		return; // waiting for user response

	std::lock_guard<std::mutex> lock(g_fwAddMutex);
	if (!g_fwPendingAdd.pending)
		return;

	// If blank firmware detected, defer to warning popup (matches Windows Confirm2 "blankFirmware")
	if (g_fwPendingAdd.isBlank) {
		g_fwPendingAdd.isBlank = false; // consume flag so we don't loop
		g_fwMgr.blankWarningState = 1; // request popup open
		return;
	}

	g_fwPendingAdd.pending = false;

	// Matches Windows ATUIDialogFirmware::Add(): the new firmware is NOT
	// committed to the manager yet.  We hand the pending info + raw data
	// off to the edit dialog in "add mode"; the dialog commits on OK and
	// discards on Cancel.
	g_fwMgr.editAddMode = true;
	g_fwMgr.pendingNewInfo = g_fwPendingAdd.info;
	g_fwMgr.pendingNewSpecific = g_fwPendingAdd.specificType;
	g_fwMgr.pendingNewData = std::move(g_fwPendingAdd.data);

	g_fwMgr.editOpen = true;
	g_fwMgr.editId = g_fwPendingAdd.info.mId;
	g_fwMgr.editType = g_fwPendingAdd.info.mType;
	g_fwMgr.editOptionInvert = (g_fwPendingAdd.info.mFlags & 1) != 0;
	snprintf(g_fwMgr.editName, sizeof(g_fwMgr.editName), "%s",
		VDTextWToU8(g_fwPendingAdd.info.mName).c_str());

	// If autodetect identified the type, preselect it in the combo.
	// Otherwise leave the combo on -1 so the dialog shows "(Unknown)"
	// and the OK button stays disabled until the user picks a real
	// type — Windows shows the same "must choose a type" behaviour.
	const auto& types = BuildFwTypeCombo();
	g_fwMgr.editTypeCombo = -1;
	if (g_fwPendingAdd.info.mType != kATFirmwareType_Unknown) {
		for (int i = 0; i < (int)types.size(); ++i) {
			if (types[i].type == g_fwPendingAdd.info.mType) {
				g_fwMgr.editTypeCombo = i;
				break;
			}
		}
	}

	g_fwPendingAdd.data.clear();
}

// Pending scan folder result — populated by folder dialog callback, consumed on main thread
static std::mutex g_fwScanMutex;
static struct {
	bool pending = false;
	VDStringW path;
} g_fwPendingScan;

// Folder dialog callback for Scan — runs on SDL dialog thread
static void FirmwareScanFolderCallback(void *userdata, const char * const *filelist, int filter) {
	(void)userdata;
	(void)filter;
	if (!filelist || !filelist[0])
		return;

	std::lock_guard<std::mutex> lock(g_fwScanMutex);
	g_fwPendingScan.pending = true;
	g_fwPendingScan.path = VDTextU8ToW(VDStringA(filelist[0]));
}

// Execute firmware scan on main thread (called when folder dialog result arrives).
// Non-static: also called from ui_mobile.cpp for mobile ROM folder selection.
extern VDStringA ATGetConfigDir();

static void TryRegisterFirmware(ATFirmwareManager *fwm,
	const VDStringW &filePath, const vdfastvector<uint8> &data,
	int &found, int &alreadyPresent)
{
	ATFirmwareInfo detInfo;
	ATSpecificFirmwareType specType = kATSpecificFirmwareType_None;
	sint32 knownIdx = -1;
	ATFirmwareDetection det = ATFirmwareAutodetect(
		data.data(), (uint32)data.size(), detInfo, specType, knownIdx);

	if (det != ATFirmwareDetection::SpecificImage)
		return;

	uint64 fwId = ATGetFirmwareIdFromPath(filePath.c_str());
	ATFirmwareInfo existing;
	if (fwm->GetFirmwareInfo(fwId, existing)) {
		++alreadyPresent;
		return;
	}

	ATFirmwareInfo newFw;
	newFw.mId = fwId;
	newFw.mPath = filePath;
	newFw.mType = detInfo.mType;
	newFw.mName = detInfo.mName.empty()
		? VDStringW(VDFileSplitPath(filePath.c_str()))
		: detInfo.mName;
	newFw.mFlags = 0;
	newFw.mbVisible = true;
	newFw.mbAutoselect = false;
	fwm->AddFirmware(newFw);

	if (specType != kATSpecificFirmwareType_None) {
		if (!fwm->GetSpecificFirmware(specType))
			fwm->SetSpecificFirmware(specType, fwId);
	}
	++found;
}

void ExecuteFirmwareScan(ATFirmwareManager *fwm, const VDStringW &scanDir) {
	VDStringW pattern = scanDir;
	if (!pattern.empty() && pattern.back() != L'/' && pattern.back() != L'\\')
		pattern += L'/';
	pattern += L"*.*";

	ATProgress progress;
	progress.InitF(100, NULL, L"Scanning for firmware");

	int found = 0, alreadyPresent = 0;
	try {
		std::vector<VDStringW> candidates;
		std::vector<VDStringW> zipFiles;
		VDDirectoryIterator it(pattern.c_str());
		while (it.Next()) {
			progress.Update(0);
			if (it.IsDirectory()) continue;
			if (it.GetAttributes() & (kVDFileAttr_System | kVDFileAttr_Hidden))
				continue;

			const wchar_t *ext = VDFileSplitExt(it.GetName());
			if (ext && ext[0] == L'.'
				&& (ext[1] == L'z' || ext[1] == L'Z')
				&& (ext[2] == L'i' || ext[2] == L'I')
				&& (ext[3] == L'p' || ext[3] == L'P')
				&& ext[4] == 0)
			{
				zipFiles.push_back(it.GetFullPath());
				continue;
			}
			if (!ATFirmwareAutodetectCheckSize(it.GetSize())) continue;
			candidates.push_back(it.GetFullPath());
		}
		progress.Update(5);

		const size_t totalItems = candidates.size() + zipFiles.size();
		size_t itemIdx = 0;

		for (size_t i = 0; i < candidates.size(); ++i, ++itemIdx) {
			if (totalItems > 0)
				progress.Update((uint32)(5 + (itemIdx * 90) / totalItems));

			const VDStringW &filePath = candidates[i];
			vdfastvector<uint8> data;
			try {
				VDFile f(filePath.c_str());
				sint64 sz = f.size();
				if (sz <= 0 || sz > 64 * 1024 * 1024) continue;
				data.resize((size_t)sz);
				f.read(data.data(), (long)sz);
				f.close();
			} catch (...) { continue; }

			TryRegisterFirmware(fwm, filePath, data, found, alreadyPresent);
		}

		if (!zipFiles.empty()) {
			VDStringA cfgDir = ATGetConfigDir();
			VDStringW fwDir = VDTextU8ToW(cfgDir);
			if (!fwDir.empty() && fwDir.back() != L'/')
				fwDir += L'/';
			fwDir += L"firmware/";

			VDStringA fwDirU8 = VDTextWToU8(fwDir);
			SDL_CreateDirectory(fwDirU8.c_str());

			for (size_t zi = 0; zi < zipFiles.size(); ++zi, ++itemIdx) {
				if (totalItems > 0)
					progress.Update((uint32)(5 + (itemIdx * 90) / totalItems));

				try {
					VDFileStream fs(zipFiles[zi].c_str());
					VDZipArchive zip;
					zip.Init(&fs);

					sint32 n = zip.GetFileCount();
					for (sint32 j = 0; j < n; j++) {
						const VDZipArchive::FileInfo &info = zip.GetFileInfo(j);
						if (!info.mbSupported) continue;
						if (!ATFirmwareAutodetectCheckSize(info.mUncompressedSize))
							continue;

						vdfastvector<uint8> data;
						try {
							bool raw = zip.ReadRawStream(j, data);
							if (!raw)
								zip.DecompressStream(j, data);
						} catch (...) { continue; }

						ATFirmwareInfo detInfo;
						ATSpecificFirmwareType specType =
							kATSpecificFirmwareType_None;
						sint32 knownIdx = -1;
						ATFirmwareDetection det = ATFirmwareAutodetect(
							data.data(), (uint32)data.size(),
							detInfo, specType, knownIdx);

						if (det != ATFirmwareDetection::SpecificImage)
							continue;

						const wchar_t *leafName = VDFileSplitPath(
							info.mDecodedFileName.c_str());
						VDStringW extractPath = fwDir + leafName;

						uint64 fwId = ATGetFirmwareIdFromPath(
							extractPath.c_str());
						ATFirmwareInfo existing;
						if (fwm->GetFirmwareInfo(fwId, existing)) {
							++alreadyPresent;
							continue;
						}

						try {
							VDFile out(extractPath.c_str(),
								nsVDFile::kWrite | nsVDFile::kDenyAll
								| nsVDFile::kCreateAlways);
							out.write(data.data(), (long)data.size());
							out.close();
						} catch (...) { continue; }

						TryRegisterFirmware(fwm, extractPath, data,
							found, alreadyPresent);
					}
				} catch (...) { continue; }
			}
		}
	} catch (const MyUserAbortError&) {
	} catch (...) {}

	progress.Shutdown();

	g_fwMgr.scanResultPending = true;
	g_fwMgr.scanResultNew = found;
	g_fwMgr.scanResultDuplicate = alreadyPresent;
}

// Last known window rect of the Firmware Manager (updated each frame during rendering).
static ImVec2 g_fwMgrWinPos = {0, 0};
static ImVec2 g_fwMgrWinSize = {0, 0};

// Handle file drop onto firmware manager (matches Windows OnDropFiles)
// Returns true if the drop was consumed (firmware manager is open and cursor is over it).
bool ATUIFirmwareManagerHandleDrop(const char *utf8path, float dropX, float dropY) {
	if (!g_showFirmwareManager || !utf8path)
		return false;

	// Check if drop position is within the Firmware Manager window
	if (dropX < g_fwMgrWinPos.x || dropY < g_fwMgrWinPos.y
		|| dropX > g_fwMgrWinPos.x + g_fwMgrWinSize.x
		|| dropY > g_fwMgrWinPos.y + g_fwMgrWinSize.y)
		return false;

	// Feed the path through the same Add callback as the file dialog
	const char *filelist[] = { utf8path, nullptr };
	FirmwareAddCallback(nullptr, filelist, 0);
	return true;
}

bool ATUIFirmwareManagerGetDropRect(ImVec2 &pos, ImVec2 &size) {
	if (!g_showFirmwareManager || g_fwMgrWinSize.x <= 0 || g_fwMgrWinSize.y <= 0)
		return false;
	pos = g_fwMgrWinPos;
	size = g_fwMgrWinSize;
	return true;
}

static void RenderFirmwareEditDialog(ATFirmwareManager *fwm) {
	if (!g_fwMgr.editOpen)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	bool open = true;
	if (ImGui::Begin("Edit Firmware Settings###FwEdit", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::InputText("Name", g_fwMgr.editName, sizeof(g_fwMgr.editName));

		// Type combo with categories
		const auto& types = BuildFwTypeCombo();
		if (!types.empty()) {
			const char *preview = (g_fwMgr.editTypeCombo >= 0 && g_fwMgr.editTypeCombo < (int)types.size())
				? types[g_fwMgr.editTypeCombo].name : "(Unknown)";
			if (ImGui::BeginCombo("Type", preview)) {
				for (const auto &cat : kFwCategories) {
					ImGui::SeparatorText(cat.categoryName);
					for (int i = 0; i < cat.count; ++i) {
						// Find index in flat list
						int flatIdx = -1;
						for (int j = 0; j < (int)types.size(); ++j)
							if (types[j].type == cat.entries[i].type) { flatIdx = j; break; }
						if (flatIdx >= 0) {
							if (ImGui::Selectable(cat.entries[i].shortName, g_fwMgr.editTypeCombo == flatIdx))
								g_fwMgr.editTypeCombo = flatIdx;
						}
					}
				}
				ImGui::EndCombo();
			}
		}

		// Show path and CRC32 (read-only).  In add-mode the firmware is not
		// yet in the manager, so read path from the pending info and CRC
		// from the buffered data; otherwise look both up via the manager.
		ATFirmwareInfo info;
		bool haveInfo = g_fwMgr.editAddMode
			? (info = g_fwMgr.pendingNewInfo, true)
			: fwm->GetFirmwareInfo(g_fwMgr.editId, info);
		if (haveInfo) {
			VDStringA pathU8 = VDTextWToU8(info.mPath);
			ImGui::TextWrapped("Path: %s", pathU8.c_str());

			static uint64 s_cachedCrcId = 0;
			static bool s_cachedCrcAddMode = false;
			static char s_cachedCrc[16] = {};
			if (s_cachedCrcId != g_fwMgr.editId || s_cachedCrcAddMode != g_fwMgr.editAddMode) {
				s_cachedCrcId = g_fwMgr.editId;
				s_cachedCrcAddMode = g_fwMgr.editAddMode;
				s_cachedCrc[0] = 0;

				const uint8 *dataPtr = nullptr;
				size_t dataLen = 0;
				vdfastvector<uint8> data;
				if (g_fwMgr.editAddMode) {
					dataPtr = g_fwMgr.pendingNewData.data();
					dataLen = g_fwMgr.pendingNewData.size();
				} else if (fwm->LoadFirmware(g_fwMgr.editId, nullptr, 0, 0, nullptr, nullptr, &data) && !data.empty()) {
					dataPtr = data.data();
					dataLen = data.size();
				}

				if (dataPtr && dataLen) {
					uint32 crc = 0xFFFFFFFF;
					for (size_t bi = 0; bi < dataLen; ++bi) {
						crc ^= dataPtr[bi];
						for (int k = 0; k < 8; ++k)
							crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
					}
					crc ^= 0xFFFFFFFF;
					snprintf(s_cachedCrc, sizeof(s_cachedCrc), "%08X", crc);
				}
			}
			if (s_cachedCrc[0])
				ImGui::InputText("CRC32", s_cachedCrc, sizeof(s_cachedCrc), ImGuiInputTextFlags_ReadOnly);
		}

		// OPTION key inverted checkbox — only for KernelXL and KernelXEGS
		// (matches Windows IDD_FIRMWARE_EDIT "OPTION key inverted" option)
		ATFirmwareType curEditType = kATFirmwareType_Unknown;
		if (g_fwMgr.editTypeCombo >= 0 && g_fwMgr.editTypeCombo < (int)types.size())
			curEditType = types[g_fwMgr.editTypeCombo].type;
		if (curEditType == kATFirmwareType_KernelXL || curEditType == kATFirmwareType_KernelXEGS) {
			ImGui::SeparatorText("Options");
			ImGui::Checkbox("OPTION key inverted (hold to enable BASIC)", &g_fwMgr.editOptionInvert);
		}

		ImGui::Separator();
		ImGui::BeginDisabled(curEditType == kATFirmwareType_Unknown);
		if (ImGui::Button("OK", ImVec2(120, 0))) {
			ATFirmwareInfo target;
			bool have = g_fwMgr.editAddMode
				? (target = g_fwMgr.pendingNewInfo, true)
				: fwm->GetFirmwareInfo(g_fwMgr.editId, target);

			if (have) {
				target.mName = VDTextU8ToW(VDStringA(g_fwMgr.editName));
				target.mType = curEditType;
				if (curEditType == kATFirmwareType_KernelXL || curEditType == kATFirmwareType_KernelXEGS)
					target.mFlags = g_fwMgr.editOptionInvert ? 1 : 0;
				else
					target.mFlags = 0;

				if (g_fwMgr.editAddMode) {
					// First-time commit for a newly-imported firmware.
					fwm->AddFirmware(target);
					if (g_fwMgr.pendingNewSpecific != kATSpecificFirmwareType_None
						&& !fwm->GetSpecificFirmware(g_fwMgr.pendingNewSpecific))
					{
						fwm->SetSpecificFirmware(g_fwMgr.pendingNewSpecific, target.mId);
					}
				} else {
					// Re-add with updated info (replaces existing by same ID)
					fwm->RemoveFirmware(g_fwMgr.editId);
					fwm->AddFirmware(target);
				}
			}

			g_fwMgr.editOpen = false;
			g_fwMgr.editAddMode = false;
			g_fwMgr.pendingNewInfo = ATFirmwareInfo{};
			g_fwMgr.pendingNewSpecific = kATSpecificFirmwareType_None;
			g_fwMgr.pendingNewData.clear();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			// Windows discards an un-committed Add on Cancel — do the same.
			g_fwMgr.editOpen = false;
			g_fwMgr.editAddMode = false;
			g_fwMgr.pendingNewInfo = ATFirmwareInfo{};
			g_fwMgr.pendingNewSpecific = kATSpecificFirmwareType_None;
			g_fwMgr.pendingNewData.clear();
		}
	}
	ImGui::End();

	if (!open) {
		g_fwMgr.editOpen = false;
		g_fwMgr.editAddMode = false;
		g_fwMgr.pendingNewInfo = ATFirmwareInfo{};
		g_fwMgr.pendingNewSpecific = kATSpecificFirmwareType_None;
		g_fwMgr.pendingNewData.clear();
	}
}

// Background audit scan thread function (matches Windows ATUIDialogKnownFirmwareAudit::ThreadRun)
// Reads user firmware files from disk and computes CRC32 to match against known firmware DB.
// File I/O is the slow part — this must NOT run on the main thread.
static void AuditScanThreadFunc() {
	vdfastvector<uint8> buf;

	for (size_t idx = 0; ; ++idx) {
		VDStringW path;
		uint64 fwId;

		{
			std::lock_guard<std::mutex> lock(g_audit.mutex);
			if (g_audit.cancelRequested || idx >= g_audit.scanItems.size())
				break;
			path = g_audit.scanItems[idx].path;
			fwId = g_audit.scanItems[idx].fwId;
		}

		sint32 knownFirmwareIndex = -1;

		try {
			VDFile f(path.c_str(), nsVDFile::kRead | nsVDFile::kOpenExisting | nsVDFile::kDenyWrite);

			{
				std::lock_guard<std::mutex> lock(g_audit.mutex);
				if (g_audit.cancelRequested) break;
			}

			const sint64 sz = f.size();
			if (ATFirmwareAutodetectCheckSize(sz)) {
				buf.resize((size_t)sz);
				f.read(buf.data(), buf.size());

				ATFirmwareInfo info;
				ATSpecificFirmwareType specFwType {};
				ATFirmwareAutodetect(buf.data(), (uint32)buf.size(), info, specFwType, knownFirmwareIndex);
			}
		} catch (...) {
		}

		{
			std::lock_guard<std::mutex> lock(g_audit.mutex);
			if (g_audit.cancelRequested) break;

			if (knownFirmwareIndex >= 0) {
				g_audit.knownToUserMatch[(size_t)knownFirmwareIndex] = g_audit.scanItems[idx].name;
			}
			g_audit.itemsScanned = idx + 1;
		}
	}

	{
		std::lock_guard<std::mutex> lock(g_audit.mutex);
		g_audit.scanComplete = true;
	}
}

// Stop any running audit scan and join the thread
static void StopAuditScan() {
#if defined(__EMSCRIPTEN__)
	// No background thread on WASM — audit runs synchronously.
	// Just clear the cancel flag for consistency.
	std::lock_guard<std::mutex> lock(g_audit.mutex);
	g_audit.cancelRequested = false;
#else
	if (g_audit.thread.joinable()) {
		{
			std::lock_guard<std::mutex> lock(g_audit.mutex);
			g_audit.cancelRequested = true;
		}
		g_audit.thread.join();
	}
#endif
}

// Start the audit: build the known firmware list (fast, main thread), then launch scan thread
static void StartAudit(ATFirmwareManager *fwm) {
	StopAuditScan();

	// Reset state
	g_audit.cancelRequested = false;
	g_audit.scanComplete = false;
	g_audit.itemsScanned = 0;
	g_audit.knownToUserMatch.clear();
	g_audit.scanItems.clear();
	g_audit.entries.clear();
	g_audit.knownIdxToEntryIdx.clear();

	// Build known firmware entry list (fast — just reads static DB, no disk I/O)
	vdvector<AuditEntry> unsorted;
	for (size_t idx = 0; ; ++idx) {
		const ATKnownFirmware *kfw = ATFirmwareGetKnownByIndex(idx);
		if (!kfw) break;

		AuditEntry ae;
		ae.typeName = GetFirmwareTypeName(kfw->mType);
		ae.description = VDTextWToU8(VDStringW(kfw->mpDesc));
		ae.size = kfw->mSize;
		ae.crc = kfw->mCRC;
		unsorted.push_back(std::move(ae));
	}

	// Sort by type name then description (same as Windows)
	// Build index mapping before sort
	vdvector<size_t> sortOrder(unsorted.size());
	for (size_t i = 0; i < sortOrder.size(); ++i) sortOrder[i] = i;
	std::sort(sortOrder.begin(), sortOrder.end(), [&](size_t a, size_t b) {
		int tc = strcasecmp(unsorted[a].typeName.c_str(), unsorted[b].typeName.c_str());
		if (tc != 0) return tc < 0;
		return strcasecmp(unsorted[a].description.c_str(), unsorted[b].description.c_str()) < 0;
	});

	g_audit.entries.resize(unsorted.size());
	for (size_t i = 0; i < sortOrder.size(); ++i) {
		size_t origIdx = sortOrder[i];
		g_audit.entries[i] = std::move(unsorted[origIdx]);
		g_audit.knownIdxToEntryIdx[origIdx] = i;
	}

	// Collect user firmware files to scan (paths + IDs — no disk I/O yet)
	vdvector<ATFirmwareInfo> fwList;
	fwm->GetFirmwareList(fwList);
	for (const auto &fw : fwList) {
		if (!fw.mbVisible || fw.mPath.empty())
			continue;
		auto &item = g_audit.scanItems.emplace_back();
		item.path = fw.mPath;
		item.fwId = fw.mId;
		item.name = VDTextWToU8(fw.mName);
	}

#if defined(__EMSCRIPTEN__)
	// WASM: run the scan inline — there are at most a couple of dozen
	// firmware files on a browser user's /persist/firmware mount, each
	// a few KB, so CRC-scanning them all completes in well under one
	// frame.  No cancellation UI is needed because the call returns
	// before the next frame repaints.
	AuditScanThreadFunc();
#else
	// Launch background scan thread
	g_audit.thread = std::thread(AuditScanThreadFunc);
#endif
}

static void RenderFirmwareAudit(ATFirmwareManager *fwm) {
	if (!g_fwMgr.auditOpen)
		return;

	// Poll scan thread for new results and update entries
	{
		std::lock_guard<std::mutex> lock(g_audit.mutex);
		for (auto &[knownIdx, userName] : g_audit.knownToUserMatch) {
			auto it = g_audit.knownIdxToEntryIdx.find(knownIdx);
			if (it != g_audit.knownIdxToEntryIdx.end()) {
				auto &entry = g_audit.entries[it->second];
				if (entry.matchedImage.empty())
					entry.matchedImage = userName;
			}
		}
	}

	ImGui::SetNextWindowSize(ImVec2(750, 500), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	bool open = true;
	if (ImGui::Begin("Known Firmware Audit###FwAudit", &open, ImGuiWindowFlags_NoSavedSettings)) {
		int matched = (int)std::count_if(g_audit.entries.begin(), g_audit.entries.end(),
			[](const AuditEntry &e) { return !e.matchedImage.empty(); });

		bool scanDone;
		size_t scanned, total;
		{
			std::lock_guard<std::mutex> lock(g_audit.mutex);
			scanDone = g_audit.scanComplete;
			scanned = g_audit.itemsScanned;
			total = g_audit.scanItems.size();
		}

		if (scanDone) {
			ImGui::Text("%d known firmware entries, %d matched in your collection.",
				(int)g_audit.entries.size(), matched);
		} else {
			ImGui::Text("%d known firmware entries, %d matched so far. Scanning %d/%d firmware files...",
				(int)g_audit.entries.size(), matched, (int)scanned, (int)total);
		}

		ImGui::Separator();

		if (ImGui::BeginTable("##AuditTable", 5,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
				| ImGuiTableFlags_Resizable,
				ImVec2(0, ImGui::GetContentRegionAvail().y - 35))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Known Firmware", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("CRC32", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Firmware Image", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin((int)g_audit.entries.size());
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					const auto &ae = g_audit.entries[i];
					ImGui::TableNextRow();
					ImGui::PushID(i);

					ImGui::TableNextColumn();
					ImGui::TextUnformatted(ae.typeName.c_str());

					ImGui::TableNextColumn();
					ImGui::TextUnformatted(ae.description.c_str());

					ImGui::TableNextColumn();
					ImGui::Text("%u", ae.size);

					ImGui::TableNextColumn();
					ImGui::Text("%08X", ae.crc);

					ImGui::TableNextColumn();
					if (!ae.matchedImage.empty()) {
						ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", ae.matchedImage.c_str());
					}

					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}

		if (ImGui::Button("OK", ImVec2(120, 0))) {
			StopAuditScan();
			g_fwMgr.auditOpen = false;
			g_audit.entries.clear();
		}
	}
	ImGui::End();

	if (!open) {
		StopAuditScan();
		g_fwMgr.auditOpen = false;
		g_audit.entries.clear();
	}
}

void RenderFirmwareManager(ATSimulator &sim, bool &show) {
	ATFirmwareManager *fwm = sim.GetFirmwareManager();
	if (!fwm) {
		show = false;
		return;
	}

	// Process any pending firmware add from file dialog callback
	ProcessPendingFirmwareAdd(fwm);

	// Process any pending scan folder result from folder dialog callback
	{
		std::lock_guard<std::mutex> lock(g_fwScanMutex);
		if (g_fwPendingScan.pending) {
			g_fwPendingScan.pending = false;
			ExecuteFirmwareScan(fwm, g_fwPendingScan.path);
		}
	}

	ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Firmware###FirmwareMgr", &show, ImGuiWindowFlags_NoSavedSettings)) {
		g_fwMgrWinPos = ImGui::GetWindowPos();
		g_fwMgrWinSize = ImGui::GetWindowSize();
		ImGui::End();
		// Clean up audit if window was closed via title bar X
		if (!show && g_fwMgr.auditOpen) {
			StopAuditScan();
			g_fwMgr.auditOpen = false;
			g_audit.entries.clear();
		}
		return;
	}

	// Save window rect for drop hit-testing
	g_fwMgrWinPos = ImGui::GetWindowPos();
	g_fwMgrWinSize = ImGui::GetWindowSize();

	if (ATUICheckEscClose()) {
		show = false;
		g_fwMgrWinSize = {0, 0};
		if (g_fwMgr.auditOpen) {
			StopAuditScan();
			g_fwMgr.auditOpen = false;
			g_audit.entries.clear();
		}
		ImGui::End();
		return;
	}

	// --- Type filter ---
	{
		const char *filterName = (g_fwMgr.filterType == kATFirmwareType_Unknown)
			? "All Types" : GetFirmwareTypeName(g_fwMgr.filterType);
		if (ImGui::BeginCombo("Firmware Type", filterName)) {
			if (ImGui::Selectable("All Types", g_fwMgr.filterType == kATFirmwareType_Unknown))
				g_fwMgr.filterType = kATFirmwareType_Unknown;
			for (const auto &cat : kFwCategories) {
				ImGui::SeparatorText(cat.categoryName);
				for (int i = 0; i < cat.count; ++i) {
					if (ImGui::Selectable(cat.entries[i].shortName, g_fwMgr.filterType == cat.entries[i].type))
						g_fwMgr.filterType = cat.entries[i].type;
				}
			}
			ImGui::EndCombo();
		}
	}

	// --- Firmware list ---
	vdvector<ATFirmwareInfo> fwList;
	fwm->GetFirmwareList(fwList);

	struct FwListEntry {
		uint64 id;		// 0 = "no firmware" placeholder
		VDStringA name;
		VDStringA typeName;
		VDStringA useFor;
		ATFirmwareType type;
		bool isCustom;
		bool isDefault;
	};

	vdvector<FwListEntry> entries;

	// Track which firmware types have entries (for "no firmware" placeholders)
	bool showType = (g_fwMgr.filterType == kATFirmwareType_Unknown);
	vdfastvector<ATFirmwareType> representedTypes;

	for (const auto &fw : fwList) {
		if (!fw.mbVisible) continue;
		if (g_fwMgr.filterType != kATFirmwareType_Unknown && fw.mType != g_fwMgr.filterType) continue;

		FwListEntry e;
		e.id = fw.mId;
		e.type = fw.mType;
		e.isCustom = (fw.mId >= kATFirmwareId_Custom);
		e.isDefault = (fwm->GetDefaultFirmware(fw.mType) == fw.mId);

		VDStringA nameU8 = VDTextWToU8(fw.mName);
		if (!fw.mPath.empty()) {
			VDStringA fnU8 = VDTextWToU8(VDStringW(VDFileSplitPath(fw.mPath.c_str())));
			e.name.sprintf("%s (%s)", nameU8.c_str(), fnU8.c_str());
		} else {
			e.name.sprintf("%s (internal)", nameU8.c_str());
		}
		e.typeName = GetFirmwareTypeName(fw.mType);
		e.useFor = BuildUseForString(GetSpecificMask(fwm, fw.mId));

		representedTypes.push_back(fw.mType);
		entries.push_back(std::move(e));
	}

	// Add "- No firmware -" placeholders for unrepresented types (matches Windows UpdateNoFirmwareNodes)
	if (showType) {
		std::sort(representedTypes.begin(), representedTypes.end());
		representedTypes.erase(std::unique(representedTypes.begin(), representedTypes.end()), representedTypes.end());

		for (const auto &cat : kFwCategories) {
			for (int i = 0; i < cat.count; ++i) {
				ATFirmwareType t = cat.entries[i].type;
				if (!std::binary_search(representedTypes.begin(), representedTypes.end(), t)) {
					FwListEntry e;
					e.id = 0;
					e.type = t;
					e.isCustom = false;
					e.isDefault = false;
					e.name = "- No firmware -";
					e.typeName = cat.entries[i].shortName;
					entries.push_back(std::move(e));
				}
			}
		}
	}

	// Sort by type short name (alphabetical), then by name within type
	std::sort(entries.begin(), entries.end(), [](const FwListEntry &a, const FwListEntry &b) {
		if (a.type != b.type) {
			int tc = strcasecmp(a.typeName.c_str(), b.typeName.c_str());
			if (tc != 0) return tc < 0;
			return (uint32)a.type < (uint32)b.type;
		}
		// Placeholders (id==0) sort last within their type
		if (!a.id && b.id) return false;
		if (a.id && !b.id) return true;
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});

	// Clamp selection
	if (g_fwMgr.selectedIdx >= (int)entries.size())
		g_fwMgr.selectedIdx = (int)entries.size() - 1;

	float tableHeight = std::max(150.0f, ImGui::GetContentRegionAvail().y - 80.0f);

	int numCols = 2 + (showType ? 1 : 0) + 1; // Name + [Type] + Use for + Default

	if (ImGui::BeginTable("##FirmwareList", numCols,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0, tableHeight))) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
		if (showType)
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("Use for", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("##default", ImGuiTableColumnFlags_WidthFixed, 20.0f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)entries.size(); ++i) {
			const auto &e = entries[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);

			// Gray out internal/built-in firmware and placeholders (matches Windows OnItemCustomStyle)
			bool grayed = !e.isCustom;
			if (grayed)
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

			ImGui::TableNextColumn();
			bool selected = (i == g_fwMgr.selectedIdx);
			if (e.id) {
				// Inline rename edit box (matches Windows label-editing
				// on custom firmware entries).  Only custom rows are
				// editable; built-in/placeholder rows fall through to
				// the regular Selectable.
				if (g_fwMgr.renameIdx == i && e.isCustom) {
					// First frame after entering rename mode: queue
					// keyboard focus for this InputText.  The grace
					// flag stays set for this frame so the cancel-on-
					// deactivation branch below cannot fire before the
					// widget has ever been active.
					const bool firstFrame = g_fwMgr.renameFocusRequested;
					if (firstFrame)
						ImGui::SetKeyboardFocusHere();

					ImGui::SetNextItemWidth(-FLT_MIN);
					const bool committed = ImGui::InputText("##rename",
						g_fwMgr.renameBuf, sizeof(g_fwMgr.renameBuf),
						ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

					// IsItemDeactivated() is edge-triggered: it only
					// fires on the frame the widget transitions from
					// active → inactive, so it's false on the initial
					// draw (widget was never active) and safe to use
					// without extra first-frame guards — but we still
					// gate on !firstFrame for extra paranoia in case a
					// future ImGui release reports it sooner.
					const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
					const bool deactivated = !firstFrame && ImGui::IsItemDeactivated();

					if (committed) {
						ATFirmwareInfo info;
						if (fwm->GetFirmwareInfo(e.id, info)) {
							VDStringW newName = VDTextU8ToW(VDStringA(g_fwMgr.renameBuf));
							if (!newName.empty() && newName != info.mName) {
								info.mName = newName;
								fwm->RemoveFirmware(e.id);
								fwm->AddFirmware(info);
							}
						}
						g_fwMgr.renameIdx = -1;
					} else if (escaped || deactivated) {
						// Cancel on Escape or when the user clicks
						// away without pressing Enter (matches Windows
						// list-view edit-abort behaviour).
						g_fwMgr.renameIdx = -1;
					}

					// Clear the focus-request flag AFTER using it so
					// deactivation detection is deferred to subsequent
					// frames.
					g_fwMgr.renameFocusRequested = false;
				} else {
					if (ImGui::Selectable(e.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
						g_fwMgr.selectedIdx = i;
					}
					// Right-click context menu (matches Windows IDR_FIRMWARE_CONTEXT_MENU)
					if (ImGui::BeginPopupContextItem("##FwCtx")) {
						g_fwMgr.selectedIdx = i;
						if (ImGui::MenuItem("Set as Default"))
							fwm->SetDefaultFirmware(e.type, e.id);
						if (e.isCustom) {
							if (ImGui::MenuItem("Rename", "F2")) {
								ATFirmwareInfo infoR;
								if (fwm->GetFirmwareInfo(e.id, infoR)) {
									g_fwMgr.renameIdx = i;
									g_fwMgr.renameFocusRequested = true;
									snprintf(g_fwMgr.renameBuf, sizeof(g_fwMgr.renameBuf),
										"%s", VDTextWToU8(infoR.mName).c_str());
								}
							}
							if (ImGui::MenuItem("Edit..."))
								OpenEditFirmwareDialog(fwm, e.id);
						}
						ImGui::EndPopup();
					}
					// Double-click opens edit (custom firmware only)
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && e.isCustom)
						OpenEditFirmwareDialog(fwm, e.id);
				}
			} else {
				// Placeholder row — not selectable (matches Windows behavior)
				ImGui::TextUnformatted(e.name.c_str());
			}

			if (showType) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(e.typeName.c_str());
			}

			ImGui::TableNextColumn();
			if (!e.useFor.empty())
				ImGui::TextUnformatted(e.useFor.c_str());

			ImGui::TableNextColumn();
			if (e.isDefault)
				ImGui::TextUnformatted("*");

			if (grayed)
				ImGui::PopStyleColor();

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// --- Buttons (matches Windows: Add, Remove, Settings, Scan, Set as Default, Use for, Audit, Clear) ---
	bool hasSel = (g_fwMgr.selectedIdx >= 0 && g_fwMgr.selectedIdx < (int)entries.size());
	bool selHasId = hasSel && entries[g_fwMgr.selectedIdx].id != 0;
	bool selCustom = selHasId && entries[g_fwMgr.selectedIdx].isCustom;

	// F2 enters inline rename on the selected custom entry (matches
	// Windows list-view default F2 shortcut).  Only triggered while the
	// firmware manager window holds focus and no inline edit is already
	// in progress.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)
		&& selCustom
		&& g_fwMgr.renameIdx < 0
		&& ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		ATFirmwareInfo info;
		if (fwm->GetFirmwareInfo(entries[g_fwMgr.selectedIdx].id, info)) {
			g_fwMgr.renameIdx = g_fwMgr.selectedIdx;
			g_fwMgr.renameFocusRequested = true;
			snprintf(g_fwMgr.renameBuf, sizeof(g_fwMgr.renameBuf), "%s",
				VDTextWToU8(info.mName).c_str());
		}
	}

	// Row 1: Add, Remove, Settings, Scan
	if (ImGui::Button("Add...")) {
		static const SDL_DialogFileFilter kRomFilters[] = {
			{ "ROM Images", "rom;bin;epr;epm" },
			{ "All Files", "*" },
		};
		ATUIShowOpenFileDialog('ROMI', FirmwareAddCallback, nullptr, nullptr, kRomFilters, 2, false);
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(!selCustom);
	if (ImGui::Button("Remove") && selCustom) {
		fwm->RemoveFirmware(entries[g_fwMgr.selectedIdx].id);
		g_fwMgr.selectedIdx = -1;
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!selCustom);
	if (ImGui::Button("Settings...") && selCustom)
		OpenEditFirmwareDialog(fwm, entries[g_fwMgr.selectedIdx].id);
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Scan...")) {
#ifdef __EMSCRIPTEN__
		// WASM has no OS folder picker; uploaded ROMs live in the
		// virtual filesystem at /home/web_user/firmware, so scan that
		// directory directly (mirrors the setup wizard's WASM path at
		// ui_tools_setup_wizard.cpp:638).
		std::lock_guard<std::mutex> lock(g_fwScanMutex);
		g_fwPendingScan.pending = true;
		g_fwPendingScan.path = VDTextU8ToW(VDStringA("/home/web_user/firmware"));
#else
		// Use SDL3 folder picker dialog (matches Windows folder browser)
		SDL_ShowOpenFolderDialog(FirmwareScanFolderCallback, nullptr, nullptr, nullptr, false);
#endif
	}

	// Row 2: Set as Default, Use for..., Audit, Clear
	ImGui::BeginDisabled(!selHasId);
	if (ImGui::Button("Set as Default") && selHasId) {
		const auto &e = entries[g_fwMgr.selectedIdx];
		fwm->SetDefaultFirmware(e.type, e.id);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!selHasId);
	if (ImGui::Button("Use for...") && selHasId)
		ImGui::OpenPopup("UseForPopup");
	ImGui::EndDisabled();

	// "Use for..." popup menu (matches Windows SetAsSpecific)
	if (ImGui::BeginPopup("UseForPopup")) {
		if (selHasId) {
			const auto &selEntry = entries[g_fwMgr.selectedIdx];
			if (ImGui::MenuItem("Clear compatibility flags")) {
				for (uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
					auto ft = (ATSpecificFirmwareType)i;
					if (fwm->GetSpecificFirmware(ft) == selEntry.id)
						fwm->SetSpecificFirmware(ft, 0);
				}
			}
			ImGui::Separator();
			for (uint32 i = 1; i < kATSpecificFirmwareTypeCount; ++i) {
				auto ft = (ATSpecificFirmwareType)i;
				if (!ATIsSpecificFirmwareTypeCompatible(selEntry.type, ft))
					continue;
				const char *label = GetSpecificFirmwareLabel(ft);
				if (!label) continue;
				char menuLabel[128];
				snprintf(menuLabel, sizeof(menuLabel), "Use for software requiring: %s", label);
				bool isAssigned = (fwm->GetSpecificFirmware(ft) == selEntry.id);
				if (ImGui::MenuItem(menuLabel, nullptr, isAssigned)) {
					if (isAssigned)
						fwm->SetSpecificFirmware(ft, 0); // toggle off
					else
						fwm->SetSpecificFirmware(ft, selEntry.id);
				}
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ImGui::Button("Audit...")) {
		g_fwMgr.auditOpen = true;
		StartAudit(fwm);
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear"))
		ImGui::OpenPopup("ClearFirmwareConfirm");

	// OK button right-aligned (matches Windows DEFPUSHBUTTON "OK")
	{
		float okWidth = 80.0f;
		float rightEdge = ImGui::GetWindowContentRegionMax().x;
		ImGui::SameLine(rightEdge - okWidth);
		if (ImGui::Button("OK", ImVec2(okWidth, 0)))
			show = false;
	}

	// Clear confirmation popup (matches Windows "This will remove all non-built-in firmware entries")
	if (ImGui::BeginPopupModal("ClearFirmwareConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::Text("This will remove all non-built-in firmware entries. Are you sure?");
		ImGui::Separator();
		if (ImGui::Button("Yes", ImVec2(120, 0))) {
			vdvector<ATFirmwareInfo> allFw;
			fwm->GetFirmwareList(allFw);
			for (const auto &fw : allFw) {
				fwm->RemoveFirmware(fw.mId); // RemoveFirmware internally skips built-in entries
			}
			g_fwMgr.selectedIdx = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Scan result feedback popup (matches Windows scan result dialog)
	if (g_fwMgr.scanResultPending) {
		ImGui::OpenPopup("ScanResultPopup");
		g_fwMgr.scanResultPending = false;
	}
	if (ImGui::BeginPopupModal("ScanResultPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		// Match Windows message format: "Firmware images recognized: N (M already present)"
		int total = g_fwMgr.scanResultNew + g_fwMgr.scanResultDuplicate;
		ImGui::Text("Firmware images recognized: %d (%d already present)", total, g_fwMgr.scanResultDuplicate);
		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Blank firmware warning popup (matches Windows Confirm2 "blankFirmware")
	if (g_fwMgr.blankWarningState == 1) {
		ImGui::OpenPopup("BlankFirmwareWarning");
		g_fwMgr.blankWarningState = 2; // popup is now open, waiting for user
	}
	if (ImGui::BeginPopupModal("BlankFirmwareWarning", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::Text("The selected file is blank and has no firmware data.");
		ImGui::Text("Use it anyway?");
		ImGui::Separator();
		if (ImGui::Button("Yes", ImVec2(120, 0))) {
			// User accepted blank firmware — allow ProcessPendingFirmwareAdd to proceed next frame
			g_fwMgr.blankWarningState = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0))) {
			// User rejected blank firmware — discard pending add
			{
				std::lock_guard<std::mutex> lock(g_fwAddMutex);
				g_fwPendingAdd.pending = false;
				g_fwPendingAdd.data.clear();
			}
			g_fwMgr.blankWarningState = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// Add-error popup (matches Windows OnDropFiles ShowError path).
	{
		bool show;
		VDStringA msg;
		{
			std::lock_guard<std::mutex> lock(g_fwAddMutex);
			show = g_fwMgr.addErrorPending;
			if (show) {
				msg = g_fwMgr.addErrorMsg;
				g_fwMgr.addErrorPending = false;
			}
		}
		static VDStringA s_lastAddErrorMsg;
		if (show) {
			s_lastAddErrorMsg = std::move(msg);
			ImGui::OpenPopup("AddFirmwareError");
		}
		if (ImGui::BeginPopupModal("AddFirmwareError", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextWrapped("%s", s_lastAddErrorMsg.c_str());
			ImGui::Separator();
			if (ImGui::Button("OK", ImVec2(120, 0)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

	// Render sub-dialogs
	RenderFirmwareEditDialog(fwm);
	RenderFirmwareAudit(fwm);

	ImGui::End();

	// If firmware manager was closed (show == false), clean up audit thread
	if (!show && g_fwMgr.auditOpen) {
		StopAuditScan();
		g_fwMgr.auditOpen = false;
		g_audit.entries.clear();
	}
}
