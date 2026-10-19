//	AltirraSDL - Menu bar rendering
//	All menu functions (File, View, System, Input, Cheat, Debug, Record,
//	Tools, Window, Help) and their associated callbacks and state.

#include <stdafx.h>
#include <mutex>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "display_backend.h"
#include "ui_file_dialog_sdl3.h"
#include "ui_search.h"

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <vd2/system/strutil.h>
#include <at/atcore/media.h>
#include <at/atcore/device.h>
#include <at/atcore/serializable.h>
#include <at/atio/image.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cartridgetypes.h>
#include <at/atio/cassetteimage.h>
#include <at/ataudio/pokey.h>

#include "ui_main.h"
#include "ui_guided_input.h"
#include "ui_menus_internal.h"
#include "ui_debugger.h"
#include "ui_textselection.h"
#include <at/atnativeui/uiframe.h>
#include "simulator.h"
#include "gtia.h"
#include "cartridge.h"
#include "cassette.h"
#include "autosavemanager.h"
#include "oshelper.h"
#include "disk.h"
#include "diskinterface.h"
#include "constants.h"
#include "uiaccessors.h"
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "accel_sdl3.h"
#include "uiclipboard.h"
#include "console.h"
#include "uitypes.h"
#include "inputmanager.h"
#include "inputcontroller.h"
#include "inputmap.h"
#include "input_selection.h"
#include "settings.h"
#include "options.h"
#include "firmwaremanager.h"

#include <algorithm>
#include <string>
#include <vector>
#include "logging.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;
extern SDL_Window *g_pWindow;      // defined in main_sdl3.cpp

// Right-click context menu for menu items — "Assign Keyboard Shortcut..."
// Call after ImGui::MenuItem() for items that have an accel command name.
// Public symbol so extracted menu files (ui_menus_view/system/debug.cpp)
// can share the same helper via ui_menus_internal.h.
void ATUIMenuShortcutContextMenu(const char *command) {
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("Assign Keyboard Shortcut..."))
			ATUIOpenShortcutEditor(command);
		ImGui::EndPopup();
	}
}

// Local shorthand so the call sites that remain in this file stay unchanged.
static inline void ShortcutContextMenu(const char *command) {
	ATUIMenuShortcutContextMenu(command);
}

// =========================================================================
// MRU (Most Recently Used) list — same registry format as Windows
// =========================================================================

void ATAddMRU(const wchar_t *path) {
	VDRegistryAppKey key("MRU List", true);

	VDStringW order;
	key.getString("Order", order);

	// Check if already in list — if so, promote to front
	VDStringW existing;
	for (size_t i = 0; i < order.size(); ++i) {
		char kn[2] = { (char)order[i], 0 };
		key.getString(kn, existing);
		if (existing.comparei(path) == 0) {
			// Promote: move to front
			wchar_t c = order[i];
			order.erase(i, 1);
			order.insert(order.begin(), c);
			key.setString("Order", order.c_str());
			return;
		}
	}

	// Not found — add new entry (recycle oldest if at 10)
	int slot = 0;
	if (order.size() >= 10) {
		wchar_t c = order.back();
		if (c >= L'A' && c < L'A' + 10)
			slot = c - L'A';
		order.resize(9);
	} else {
		slot = (int)order.size();
	}

	order.insert(order.begin(), L'A' + slot);
	char kn[2] = { (char)('A' + slot), 0 };
	key.setString(kn, path);
	key.setString("Order", order.c_str());
}

static VDStringW ATGetMRU(uint32 index) {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	VDStringW s;
	if (index < order.size()) {
		char kn[2] = { (char)order[index], 0 };
		key.getString(kn, s);
	}
	return s;
}

static uint32 ATGetMRUCount() {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	return (uint32)order.size();
}

static void ATClearMRU() {
	VDRegistryAppKey key("MRU List", true);
	key.removeValue("Order");
}

// =========================================================================
// File dialog callbacks (SDL3 async)
// =========================================================================

static void BootImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0] || !*filelist[0]) return;
	ATUIPushDeferred(kATDeferred_BootImage, filelist[0]);
}

static void OpenImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0] || !*filelist[0]) return;
	ATUIPushDeferred(kATDeferred_OpenImage, filelist[0]);
}

static void CartridgeAttachCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0] || !*filelist[0]) return;
	ATUIPushDeferred(kATDeferred_AttachCartridge, filelist[0]);
}

// Per-drive attach callback — drive index in userdata
static void AttachDiskCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || !*filelist[0] || driveIdx < 0 || driveIdx >= 15) return;
	ATUIPushDeferred(kATDeferred_AttachDisk, filelist[0], driveIdx);
}

static void CassetteSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0] || !*filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveCassette, filelist[0]);
}

// ATUISaveFrameCallback is declared in ui_main.h, defined in ui_main.cpp

// =========================================================================
// State save/load
// =========================================================================

static vdrefptr<IATSerializable> g_pQuickSaveState;

static void SaveStateCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveState, filelist[0]);
}

static void LoadStateCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_LoadState, filelist[0]);
}

// Shared menu/search actions; preserve the native state-dialog filters.
void ATUIShowStateFileDialog(SDL_Window *window, bool save) {
	static const SDL_DialogFileFilter filters[] = {
		{ "Save States", "atstate2;atstate" }, { "All Files", "*" },
	};
	if (save)
		ATUIShowSaveFileDialog('save', SaveStateCallback, nullptr, window, filters, 1);
	else
		ATUIShowOpenFileDialog('save', LoadStateCallback, nullptr, window, filters, 2, false);
}

void ATUIQuickSaveState() {
	try {
		vdrefptr<IATSerializable> info;
		g_sim.CreateSnapshot(~g_pQuickSaveState, ~info);
		LOG_INFO("UI", "Quick save state created");
	} catch (...) {
		LOG_ERROR("UI", "Quick save failed");
	}
}

void ATUIQuickLoadState() {
	if (!g_pQuickSaveState)
		return;
	try {
		g_sim.ApplySnapshot(*g_pQuickSaveState, nullptr);
		g_sim.Resume();
		LOG_INFO("UI", "Quick load state applied");
	} catch (...) {
		LOG_ERROR("UI", "Quick load failed");
	}
}

bool ATUIHasQuickSaveState() {
	return g_pQuickSaveState != nullptr;
}

// =========================================================================
// File dialog filters
// =========================================================================

static const SDL_DialogFileFilter kImageFilters[] = {
	{ "Atari Images", "atr;xfd;dcm;xex;obx;com;bin;rom;car;cas;wav;gz;zip;atz" },
	{ "All Files", "*" },
};

// =========================================================================
// Public functions for keyboard shortcut wiring (called from main_sdl3.cpp)
// =========================================================================

void ATUIShowBootImageDialog(SDL_Window *window) {
	ATUIShowOpenFileDialog('load', BootImageCallback, nullptr, window, kImageFilters, 2, false);
}

void ATUIShowOpenImageDialog(SDL_Window *window) {
	ATUIShowOpenFileDialog('load', OpenImageCallback, nullptr, window, kImageFilters, 2, false);
}

static std::mutex g_pendingSourceMutex;
static std::string g_pendingSourcePath;

void ATUIShowOpenSourceFileDialog(SDL_Window *window) {
	static const SDL_DialogFileFilter srcFilters[] = {
		{ "Source Files", "s;asm;src;lst;inc;txt" },
		{ "All Files", "*" },
	};
	// SDL3 file dialog callbacks may fire on a background thread.
	// Store the path and process on the main thread.
	ATUIShowOpenFileDialog('src ', [](void *, const char * const *fl, int) {
		if (fl && fl[0]) {
			std::lock_guard<std::mutex> lock(g_pendingSourceMutex);
			g_pendingSourcePath = fl[0];
		}
	}, nullptr, window, srcFilters, 2, false);
}

void ATUIShowSaveFrameDialog(SDL_Window *window, bool trueAspect) {
	g_saveFrameTrueAspect = trueAspect;
	static const SDL_DialogFileFilter filters[] = {
		{ "PNG Images", "png" },
	};
	ATUIShowSaveFileDialog('scrn', ATUISaveFrameCallback, nullptr, window, filters, 1);
}

void ATUIPasteText() {
	VDStringW clipText;
	if (!ATUIClipGetText(clipText) || clipText.empty())
		return;
	ATUIPasteTextDirect(clipText.data(), clipText.size());
}

static const SDL_DialogFileFilter kCartFilters[] = {
	{ "Cartridge Images", "car;rom;bin" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kDiskAttachFilters[] = {
	{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kCasSaveFilters[] = {
	{ "Cassette Images", "cas" },
	{ "All Files", "*" },
};

// =========================================================================
// File menu
// =========================================================================


// =========================================================================
// File menu
// =========================================================================

static void RenderFileMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	if (ImGui::MenuItem("Boot Image...", ATUIGetShortcutStringForCommand("File.BootImage")))
		ATUIShowOpenFileDialog('load', BootImageCallback, nullptr, window, kImageFilters, 2, false);
	ShortcutContextMenu("File.BootImage");

	if (ImGui::MenuItem("Open Image...", ATUIGetShortcutStringForCommand("File.OpenImage")))
		ATUIShowOpenImageDialog(window);
	ShortcutContextMenu("File.OpenImage");

	if (ImGui::MenuItem("Game Library..."))
		state.showGameLibrary = true;

	// Recently Booted (MRU list)
	uint32 mruCount = ATGetMRUCount();
	if (ImGui::BeginMenu("Recently Booted", mruCount > 0)) {
		for (uint32 i = 0; i < mruCount; ++i) {
			VDStringW wpath = ATGetMRU(i);
			if (wpath.empty()) continue;
			VDStringA u8 = VDTextWToU8(wpath);
			const char *base = strrchr(u8.c_str(), '/');
			if (!base) base = strrchr(u8.c_str(), '\\');
			const char *label = base ? base + 1 : u8.c_str();

			char menuLabel[280];
			snprintf(menuLabel, sizeof(menuLabel), "%d. %s", i + 1, label);

			if (ImGui::MenuItem(menuLabel)) {
				ATImageLoadContext ctx {};
				if (g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx)) {
					ATAddMRU(wpath.c_str());
					g_sim.ColdReset();
					g_sim.Resume();
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", u8.c_str());
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Clear List"))
			ATClearMRU();

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Disk Drives...", ATUIGetShortcutStringForCommand("Disk.DrivesDialog")))
		state.showDiskManager = true;
	ShortcutContextMenu("Disk.DrivesDialog");

	// Attach Disk submenu (matches Windows: Rotate + D1-D8)
	if (ImGui::BeginMenu("Attach Disk")) {
		if (ImGui::MenuItem("Rotate Down"))
			sim.RotateDrives(8, 1);
		if (ImGui::MenuItem("Rotate Up"))
			sim.RotateDrives(8, -1);
		// Extension over Windows Altirra (approved): swap the disks
		// mounted in drive 1 (index 0) and drive 2 (index 1).
		if (ImGui::MenuItem("Swap Disks"))
			sim.SwapDrives(0, 1);

		ImGui::Separator();

		for (int i = 0; i < 8; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "Drive %d...", i + 1);

			ATDiskInterface& di = sim.GetDiskInterface(i);
			bool loaded = di.IsDiskLoaded();

			// Show current image in menu item if loaded
			if (loaded) {
				const wchar_t *path = di.GetPath();
				VDStringA u8;
				if (path && *path) {
					u8 = VDTextWToU8(VDStringW(path));
					const char *base = strrchr(u8.c_str(), '/');
					if (base) u8 = VDStringA(base + 1);
				}
				char fullLabel[256];
				snprintf(fullLabel, sizeof(fullLabel), "Drive %d [%s]...",
					i + 1, u8.empty() ? "loaded" : u8.c_str());
				if (ImGui::MenuItem(fullLabel))
					ATUIShowOpenFileDialog('disk', AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, false);
			} else {
				if (ImGui::MenuItem(label))
					ATUIShowOpenFileDialog('disk', AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, false);
			}
		}
		ImGui::EndMenu();
	}

	// Detach Disk submenu (matches Windows: All + D1-D8)
	if (ImGui::BeginMenu("Detach Disk")) {
		if (ImGui::MenuItem("All")) {
			for (int i = 0; i < 15; ++i) {
				sim.GetDiskInterface(i).UnloadDisk();
				sim.GetDiskDrive(i).SetEnabled(false);
			}
		}

		ImGui::Separator();

		for (int i = 0; i < 8; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "Drive %d", i + 1);
			bool loaded = sim.GetDiskInterface(i).IsDiskLoaded();
			if (ImGui::MenuItem(label, nullptr, false, loaded)) {
				sim.GetDiskInterface(i).UnloadDisk();
				sim.GetDiskDrive(i).SetEnabled(false);
			}
		}
		ImGui::EndMenu();
	}

	// Cassette submenu (matches Windows: Tape Control, Tape Editor, New/Load/Unload/Save/Export)
	if (ImGui::BeginMenu("Cassette")) {
		ATCassetteEmulator& cas = sim.GetCassette();
		bool loaded = cas.IsLoaded();

		if (ImGui::MenuItem("Tape Control..."))
			state.showCassetteControl = true;

		if (ImGui::MenuItem("Tape Editor..."))
			state.showTapeEditor = true;

		ImGui::Separator();

		if (ImGui::MenuItem("New Tape"))
			cas.LoadNew();

		if (ImGui::MenuItem("Load...")) {
			static const SDL_DialogFileFilter casFilters[] = {
				{ "Cassette Images", "cas;wav" }, { "All Files", "*" },
			};
			ATUIShowOpenFileDialog('cass', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_LoadCassette, fl[0]);
			}, nullptr, window, casFilters, 2, false);
		}

		if (ImGui::MenuItem("Unload", nullptr, false, loaded))
			cas.Unload();

		if (ImGui::MenuItem("Save...", nullptr, false, loaded)) {
			ATUIShowSaveFileDialog('cass', CassetteSaveCallback, nullptr, window,
				kCasSaveFilters, 2);
		}

		if (ImGui::MenuItem("Export Audio Tape...", nullptr, false, loaded)) {
			static const SDL_DialogFileFilter wavFilters[] = {
				{ "WAV Audio", "wav" }, { "All Files", "*" },
			};
			ATUIShowSaveFileDialog('casa', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_ExportCassetteAudio, fl[0]);
			}, nullptr, window, wavFilters, 1);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// State save/load
	{
		if (ImGui::MenuItem("Load State..."))
			ATUIShowStateFileDialog(window, false);

		if (ImGui::MenuItem("Save State..."))
			ATUIShowStateFileDialog(window, true);

		if (ImGui::MenuItem("Quick Load State", nullptr, false, g_pQuickSaveState != nullptr))
			ATUIQuickLoadState();

		if (ImGui::MenuItem("Quick Save State"))
			ATUIQuickSaveState();
	}

	ImGui::Separator();

	// Attach Special Cartridge submenu (matches Windows exactly)
	if (ImGui::BeginMenu("Attach Special Cartridge")) {
		static const struct { const char *label; int mode; } kSpecialCarts[] = {
			{ "SuperCharger3D",                                     kATCartridgeMode_SuperCharger3D },
			{ "Empty 128K (1Mbit) MaxFlash cartridge",              kATCartridgeMode_MaxFlash_128K },
			{ "Empty 128K (1Mbit) MaxFlash cartridge (MyIDE banking)", kATCartridgeMode_MaxFlash_128K_MyIDE },
			{ "Empty 1M (8Mbit) MaxFlash cartridge (older - bank 127)", kATCartridgeMode_MaxFlash_1024K },
			{ "Empty 1M (8Mbit) MaxFlash cartridge (newer - bank 0)", kATCartridgeMode_MaxFlash_1024K_Bank0 },
			{ "Empty 128K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_128K },
			{ "Empty 256K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_256K },
			{ "Empty 512K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_512K },
			{ "Empty 1024K J(Atari)Cart",                           kATCartridgeMode_JAtariCart_1024K },
			{ "Empty DCart",                                        kATCartridgeMode_DCart },
			{ "Empty SIC! 512K flash cartridge",                    kATCartridgeMode_SIC_512K },
			{ "Empty SIC! 256K flash cartridge",                    kATCartridgeMode_SIC_256K },
			{ "Empty SIC! 128K flash cartridge",                    kATCartridgeMode_SIC_128K },
			{ "Empty SIC+ flash cartridge",                         kATCartridgeMode_SICPlus },
			{ "Empty 512K MegaCart flash cartridge",                kATCartridgeMode_MegaCart_512K_3 },
			{ "Empty 4MB MegaCart flash cartridge",                 kATCartridgeMode_MegaCart_4M_3 },
			{ "Empty The!Cart 32MB flash cartridge",                kATCartridgeMode_TheCart_32M },
			{ "Empty The!Cart 64MB flash cartridge",                kATCartridgeMode_TheCart_64M },
			{ "Empty The!Cart 128MB flash cartridge",               kATCartridgeMode_TheCart_128M },
		};

		for (auto& sc : kSpecialCarts) {
			if (ImGui::MenuItem(sc.label)) {
				sim.LoadNewCartridge(sc.mode);
				if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
					sim.ColdReset();
				sim.Resume();
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("BASIC")) {
			sim.LoadCartridgeBASIC();
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				sim.ColdReset();
			sim.Resume();
		}

		ImGui::EndMenu();
	}

	// Secondary Cartridge submenu
	if (ImGui::BeginMenu("Secondary Cartridge")) {
		if (ImGui::MenuItem("Attach..."))
			ATUIShowOpenFileDialog('cart', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_AttachSecondaryCartridge, fl[0]);
			}, nullptr, window, kCartFilters, 2, false);

		if (ImGui::MenuItem("Detach", nullptr, false, sim.IsCartridgeAttached(1))) {
			sim.UnloadCartridge(1);
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				sim.ColdReset();
			sim.Resume();
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Attach Cartridge..."))
		ATUIShowOpenFileDialog('cart', CartridgeAttachCallback, nullptr, window, kCartFilters, 2, false);

	if (ImGui::MenuItem("Detach Cartridge", nullptr, false, sim.IsCartridgeAttached(0))) {
		if (sim.GetHardwareMode() == kATHardwareMode_5200)
			sim.LoadCartridge5200Default();
		else
			sim.UnloadCartridge(0);
		if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
			sim.ColdReset();
		sim.Resume();
	}

	// Save Firmware submenu
	if (ImGui::BeginMenu("Save Firmware")) {
		if (ImGui::MenuItem("Save Cartridge...", nullptr, false, sim.IsCartridgeAttached(0))) {
			static const SDL_DialogFileFilter cartSaveFilters[] = {
				{ "Cartridge image with header (*.car)", "car" },
				{ "Raw cartridge image (*.bin, *.rom)", "bin;rom" },
				{ "All Files", "*" },
			};
			ATUIShowSaveFileDialog('cart', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveCartridge, fl[0]);
			}, nullptr, window, cartSaveFilters, 3);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE / SIDE / MyIDE II Main Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)kATStorageId_Firmware))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)0, window, fwFilters, 2);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE SDX Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 1)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)1, window, fwFilters, 2);
		}

		if (ImGui::MenuItem("Save Ultimate1MB Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 2)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)2, window, fwFilters, 2);
		}

		if (ImGui::MenuItem("Save Rapidus Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 3)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)3, window, fwFilters, 2);
		}

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Exit")) {
		if (!state.showExitConfirm)
			state.showExitConfirm = true;
	}
}


// =========================================================================
// Input menu
// =========================================================================

// Render a single Port submenu.  Queries ATInputManager for all input maps
// that touch the given physical port, presents them as independent checked
// items, and persists each selection immediately.
static void RenderPortSubmenu(ATInputManager &im, int portIdx) {
	// Fork extension: convenient entry to the same guided map editor.
	if (portIdx < 2) {
		if (ImGui::MenuItem("Guided Joystick Setup...", nullptr, false, !im.Is5200Mode()))
			ATUIOpenGuidedJoystickSetup(portIdx);
		ImGui::Separator();
	}
	// Collect input maps that use this physical port
	struct MapEntry {
		ATInputMap *map;
		VDStringA  name;   // UTF-8 for ImGui
		bool       active;
	};
	std::vector<MapEntry> entries;

	uint32 mapCount = im.GetInputMapCount();
	for (uint32 i = 0; i < mapCount; ++i) {
		vdrefptr<ATInputMap> imap;
		if (im.GetInputMapByIndex(i, ~imap)) {
			if (imap->UsesPhysicalPort(portIdx)) {
				MapEntry e;
				e.map = imap;
				e.name = VDTextWToU8(imap->GetName(), -1);
				e.active = im.IsInputMapEnabled(imap);
				entries.push_back(std::move(e));
			}
		}
	}

	// Sort alphabetically (case-insensitive), matching Windows
	std::sort(entries.begin(), entries.end(),
		[](const MapEntry &a, const MapEntry &b) {
			return vdwcsicmp(a.map->GetName(), b.map->GetName()) < 0;
		});

	// "None" item — selected when no maps are active for this port
	bool anyActive = false;
	for (const auto &e : entries)
		if (e.active) { anyActive = true; break; }

	if (ImGui::RadioButton("None", !anyActive))
		ATInputSelection::ClearPort(im, portIdx);

	// Each source is independently selectable.  Keep the parent menu open so
	// a player can choose Arrow Keys, Numpad, and a gamepad in one visit.
	for (const auto &e : entries) {
		bool enabled = e.active;
		ImGui::PushID(e.map);
		if (ImGui::Checkbox(e.name.c_str(), &enabled))
			ATInputSelection::Toggle(im, e.map);
		ImGui::PopID();
	}
}

static void RenderInputMenu(ATSimulator &sim, ATUIState &state) {
	ATInputManager *pIM = sim.GetInputManager();

	if (ImGui::MenuItem("Input Mappings..."))
		state.showInputMappings = true;
	if (ImGui::MenuItem("Input Setup..."))
		state.showInputSetup = true;

	// Cycle Quick Maps — cycles through maps marked as quick-cycle
	if (ImGui::MenuItem("Cycle Quick Maps", ATUIGetShortcutStringForCommand("Input.CycleQuickMaps"))) {
		if (pIM) {
			ATInputMap *pMap = pIM->CycleQuickMaps();
			ATInputSelection::CommitSelections();
			if (pMap) {
				VDStringA msg;
				msg = "Quick map: ";
				msg += VDTextWToU8(pMap->GetName(), -1);
				LOG_INFO("UI", "%s", msg.c_str());
			} else {
				LOG_INFO("UI", "Quick maps disabled");
			}
		}
	}

	ImGui::Separator();

	// Capture Mouse — only enabled when mouse is mapped in an input map
	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool captured = ATUIIsMouseCaptured();
		if (ImGui::MenuItem("Capture Mouse", ATUIGetShortcutStringForCommand("Input.CaptureMouse"), captured, mouseMapped)) {
			if (captured)
				ATUIReleaseMouse();
			else
				ATUICaptureMouse();
		}
	}

	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool mouseAutoCapture = ATUIGetMouseAutoCapture();
		if (ImGui::MenuItem("Auto-Capture Mouse", nullptr, mouseAutoCapture, mouseMapped))
			ATUISetMouseAutoCapture(!mouseAutoCapture);
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Virtual Keyboard",
		ATUIGetShortcutStringForCommand("Input.VirtualKeyboard"),
		state.showVirtualKeyboard))
		state.showVirtualKeyboard = !state.showVirtualKeyboard;

	if (ImGui::BeginMenu("Keyboard Placement")) {
		if (ImGui::MenuItem("Auto", nullptr, state.oskPlacement == 0))
			state.oskPlacement = 0;
		if (ImGui::MenuItem("Bottom", nullptr, state.oskPlacement == 1))
			state.oskPlacement = 1;
		if (ImGui::MenuItem("Right", nullptr, state.oskPlacement == 2))
			state.oskPlacement = 2;
		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Light Pen/Gun..."))
		state.showLightPen = true;
	if (ImGui::MenuItem("Recalibrate Light Pen/Gun"))
		ATUIRecalibrateLightPen();

	ImGui::Separator();

	// Port 1-4 submenus — dynamic input map assignment per port
	if (pIM) {
		for (int i = 0; i < 4; ++i) {
			char label[16];
			snprintf(label, sizeof(label), "Port %d", i + 1);
			if (ImGui::BeginMenu(label)) {
				RenderPortSubmenu(*pIM, i);
				ImGui::EndMenu();
			}
		}
	}
}

// =========================================================================
// Cheat menu
// =========================================================================

static void RenderCheatMenu(ATSimulator &sim, ATUIState &state) {
	if (ImGui::MenuItem("Cheater...", ATUIGetShortcutStringForCommand("Cheat.CheatDialog")))
		state.showCheater = true;
	ImGui::Separator();

	ATGTIAEmulator& gtia = sim.GetGTIA();

	bool pmColl = gtia.ArePMCollisionsEnabled();
	if (ImGui::MenuItem("Disable P/M Collisions", nullptr, !pmColl))
		gtia.SetPMCollisionsEnabled(!pmColl);

	bool pfColl = gtia.ArePFCollisionsEnabled();
	if (ImGui::MenuItem("Disable Playfield Collisions", nullptr, !pfColl))
		gtia.SetPFCollisionsEnabled(!pfColl);
}


// =========================================================================
// Record menu
// =========================================================================

void ATUIRecordRawAudio(SDL_Window *window) {
	static const SDL_DialogFileFilter rawFilters[] = {
		{ "Raw PCM Audio", "pcm" }, { "All Files", "*" },
	};
	ATUIShowSaveFileDialog('raud', [](void *, const char * const *fl, int) {
		if (fl && fl[0])
			ATUIPushDeferred(kATDeferred_StartRecordRaw, fl[0]);
	}, nullptr, window, rawFilters, 1);
}

void ATUIRecordAudio(SDL_Window *window) {
	static const SDL_DialogFileFilter wavFilters[] = {
		{ "WAV Audio", "wav" }, { "All Files", "*" },
	};
	ATUIShowSaveFileDialog('raud', [](void *, const char * const *fl, int) {
		if (fl && fl[0])
			ATUIPushDeferred(kATDeferred_StartRecordWAV, fl[0]);
	}, nullptr, window, wavFilters, 1);
}

void ATUIRecordSAP(SDL_Window *window) {
	static const SDL_DialogFileFilter sapFilters[] = {
		{ "SAP Files", "sap" }, { "All Files", "*" },
	};
	ATUIShowSaveFileDialog('rsap', [](void *, const char * const *fl, int) {
		if (fl && fl[0])
			ATUIPushDeferred(kATDeferred_StartRecordSAP, fl[0]);
	}, nullptr, window, sapFilters, 1);
}

void ATUIRecordVGM(SDL_Window *window) {
	static const SDL_DialogFileFilter vgmFilters[] = {
		{ "VGM Audio", "vgm" }, { "All Files", "*" },
	};
	ATUIShowSaveFileDialog('rvgm', [](void *, const char * const *fl, int) {
		if (fl && fl[0])
			ATUIPushDeferred(kATDeferred_StartRecordVGM, fl[0]);
	}, nullptr, window, vgmFilters, 1);
}

static void RenderRecordMenu(ATSimulator &sim, SDL_Window *window) {
	bool recording = ATUIIsRecording();

	if (ImGui::MenuItem("Record Raw Audio...", nullptr, false, !recording))
		ATUIRecordRawAudio(window);
	if (ImGui::MenuItem("Record Audio...", nullptr, false, !recording))
		ATUIRecordAudio(window);
	if (ImGui::MenuItem("Record Video...", nullptr, ATUIIsVideoRecording(), !recording))
		ATUIShowVideoRecordingDialog();

	if (ImGui::MenuItem("Record SAP Type R...", nullptr, false, !recording))
		ATUIRecordSAP(window);
	if (ImGui::MenuItem("Record VGM...", nullptr, false, !recording))
		ATUIRecordVGM(window);
	ImGui::Separator();

	if (ImGui::MenuItem("Stop Recording", nullptr, false, recording))
		ATUIStopRecording();

	if (ImGui::MenuItem("Pause/Resume Recording", nullptr, false, ATUIIsVideoRecording()))
		ATUIToggleRecordingPause();
}

// =========================================================================
// Tools menu
// =========================================================================

// SAP-to-EXE: two-step file dialog (open SAP, then save XEX).
// File dialog callbacks may run on non-main threads, but
// SDL_ShowSaveFileDialog MUST be called from the main thread.
// So the open callback just stores the path; the main thread
// (in RenderToolsMenu) opens the save dialog.
static std::mutex g_toolsDialogMutex;
static std::string g_sapSourcePath;
static bool g_sapNeedsSaveDialog = false;

static void SAPSaveCallback(void *, const char * const *filelist, int) {
	std::lock_guard<std::mutex> lock(g_toolsDialogMutex);
	if (!filelist || !filelist[0] || g_sapSourcePath.empty())
		return;
	ATUIPushDeferred2(kATDeferred_ConvertSAPToEXE, g_sapSourcePath.c_str(), filelist[0]);
	g_sapSourcePath.clear();
}

static void SAPOpenCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(g_toolsDialogMutex);
	g_sapSourcePath = filelist[0];
	g_sapNeedsSaveDialog = true;
}

// Export ROM Set: folder dialog callback
static void ExportROMSetCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	ATUIPushDeferred(kATDeferred_ExportROMSet, filelist[0]);
}

// Analyze Tape: two-step file dialog (open WAV, then save WAV)
static std::string g_tapeSourcePath;
static bool g_tapeNeedsSaveDialog = false;

static void TapeAnalysisSaveCallback(void *, const char * const *filelist, int) {
	std::lock_guard<std::mutex> lock(g_toolsDialogMutex);
	if (!filelist || !filelist[0] || g_tapeSourcePath.empty())
		return;
	ATUIPushDeferred2(kATDeferred_AnalyzeTapeDecode, g_tapeSourcePath.c_str(), filelist[0]);
	g_tapeSourcePath.clear();
}

static void TapeAnalysisOpenCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	std::lock_guard<std::mutex> lock(g_toolsDialogMutex);
	g_tapeSourcePath = filelist[0];
	g_tapeNeedsSaveDialog = true;
}

static void RenderToolsMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	if (ImGui::BeginMenu("Explorers")) {
		if (ImGui::MenuItem("Disk Explorer..."))
			ATUIRequestDiskExplorer(window);
		if (ImGui::MenuItem("Cartridge Explorer..."))
			ATUIRequestCartridgeExplorer(window);
		if (ImGui::MenuItem("XEX Explorer..."))
			ATUIRequestXEXExplorer(window);
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Convert SAP to EXE...")) {
		static const SDL_DialogFileFilter kSAPFilters[] = {
			{ "SAP Music Files", "sap" },
			{ "All Files", "*" },
		};
		ATUIShowOpenFileDialog('sap ', SAPOpenCallback, window, window, kSAPFilters, 2, false);
	}

	if (ImGui::MenuItem("Export ROM set..."))
		ATUIShowOpenFolderDialog('rom ', ExportROMSetCallback, nullptr,
			window);

	if (ImGui::MenuItem("Analyze tape decoding...")) {
		static const SDL_DialogFileFilter kTapeFilters[] = {
			{ "Audio Files", "wav;flac" },
			{ "All Files", "*" },
		};
		ATUIShowOpenFileDialog('cass', TapeAnalysisOpenCallback, window, window, kTapeFilters, 2, false);
	}

	// Debug Log — viewer for ATLogChannel output (netplay, disk,
	// audio, cassette, video, ...).  Stderr is unreachable on Android
	// and clipped on macOS app bundles; this is the only way to see
	// the live log from inside the app on those platforms.  Lives in
	// Tools alongside the other runtime diagnostics rather than in
	// About, which is reserved for credits / version / configuration
	// summary.
	if (ImGui::MenuItem("Debug Log..."))
		state.showDebugLog = true;

	ImGui::Separator();

	if (ImGui::MenuItem("First Time Setup...")) {
		extern void Wiz_Open(ATUIState &);
		Wiz_Open(state);
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Keyboard Shortcuts..."))
		state.showKeyboardShortcuts = true;

	if (ImGui::MenuItem("Compatibility Database..."))
		state.showCompatDB = true;

	if (ImGui::MenuItem("Advanced Configuration..."))
		state.showAdvancedConfig = true;
}

// =========================================================================
// Window menu
// =========================================================================

static void RenderWindowMenu(SDL_Window *window) {
	{
		bool dbgOpen = ATUIDebuggerIsOpen();
		bool hasPanes = dbgOpen && ATUIDebuggerHasVisiblePanes();
		bool hasFocus = dbgOpen && ATUIDebuggerGetFocusedPaneId() != 0;

		if (ImGui::MenuItem("Close", nullptr, false, hasFocus))
			ATUIDebuggerCloseActivePane();
		if (ImGui::MenuItem("Undock", nullptr, false, hasFocus))
			ATUIDebuggerUndockActivePane();
		if (ImGui::MenuItem("Next Pane", nullptr, false, hasPanes))
			ATUIDebuggerCyclePane(+1);
		if (ImGui::MenuItem("Previous Pane", nullptr, false, hasPanes))
			ATUIDebuggerCyclePane(-1);
	}
	ImGui::Separator();

	if (ImGui::BeginMenu("Adjust Window Size")) {
		static const struct { const char *label; int w; int h; } kSizes[] = {
			{ "1x (336x240)", 336, 240 },
			{ "2x (672x480)", 672, 480 },
			{ "3x (1008x720)", 1008, 720 },
			{ "4x (1344x960)", 1344, 960 },
		};
		for (auto& sz : kSizes) {
			if (ImGui::MenuItem(sz.label))
				SDL_SetWindowSize(window, sz.w, sz.h);
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Reset Window Layout")) {
		// Reset to default 2x size
		SDL_SetWindowSize(window, 672, 480);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
}

// =========================================================================
// Help menu
// =========================================================================

static void RenderHelpMenu(ATUIState &state) {
	if (ImGui::MenuItem("Search actions and settings...",
		ATUIGetShortcutStringForCommand("UI.GlobalSearch")))
		ATUIOpenGlobalSearch();
	ImGui::Separator();
	if (ImGui::MenuItem("Contents"))
		state.showHelpContents = true;

	if (ImGui::MenuItem("About"))
		state.showAboutDialog = true;

	if (ImGui::MenuItem("Change Log"))
		state.showChangeLog = true;

	if (ImGui::MenuItem("Command-Line Help"))
		state.showCommandLineHelp = true;

	if (ImGui::MenuItem("Export Debugger Help..."))
		ATUIExportDebugHelp();
	ImGui::MenuItem("Check For Updates", nullptr, false, false);       // placeholder — N/A on Linux
	if (ImGui::MenuItem("Altirra Home..."))
		ATLaunchURL(L"https://www.virtualdub.org/altirra.html");
}

// =========================================================================
// Main menu bar
// =========================================================================

// Menu bar height, updated each frame.  Used by ComputeDisplayRect() in
// main_sdl3.cpp to position the emulator display below the menu bar.
extern float g_menuBarHeight;

// Process deferred file dialog results.  Must run on the main thread every
// frame regardless of whether the ImGui menu bar is rendered (e.g. on macOS
// the native menu bar replaces the ImGui one, but dialogs still need
// processing).
void ATUIProcessDeferredMenuDialogs(SDL_Window *window) {
	// Process deferred source file open (must run on main thread).
	{
		std::lock_guard<std::mutex> lock(g_pendingSourceMutex);
		if (!g_pendingSourcePath.empty()) {
			VDStringW wpath = VDTextU8ToW(VDStringA(g_pendingSourcePath.c_str()));
			g_pendingSourcePath.clear();
			ATOpenSourceWindow(wpath.c_str());
		}
	}

	// Process deferred save dialogs from file dialog callbacks (must run on main thread).
	// This runs every frame regardless of whether the Tools menu is open.
	{
		std::lock_guard<std::mutex> lock(g_toolsDialogMutex);
		if (g_sapNeedsSaveDialog) {
			g_sapNeedsSaveDialog = false;
			static const SDL_DialogFileFilter kXEXFilters[] = {
				{ "Atari Executable", "xex;obx;com" },
				{ "All Files", "*" },
			};
			ATUIShowSaveFileDialog('sap ', SAPSaveCallback, nullptr, window, kXEXFilters, 2);
		}
		if (g_tapeNeedsSaveDialog) {
			g_tapeNeedsSaveDialog = false;
			static const SDL_DialogFileFilter kWAVFilters[] = {
				{ "WAV Audio", "wav" },
				{ "All Files", "*" },
			};
			ATUIShowSaveFileDialog('casa', TapeAnalysisSaveCallback, nullptr, window, kWAVFilters, 2);
		}
	}
}

void ATUIRenderMainMenu(ATSimulator &sim, SDL_Window *window, IDisplayBackend *backend, ATUIState &state) {
#ifdef VD_OS_MACOS
	// On macOS, the native NSMenu bar replaces the ImGui menu bar.
	// No ImGui rendering needed — the display fills the entire window.
	g_menuBarHeight = 0.0f;
#else
	// In fullscreen mode, hide the menu bar to match Windows Altirra behaviour.
	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (isFullscreen) {
		g_menuBarHeight = 0.0f;
		ATUIProcessDeferredMenuDialogs(window);
		return;
	}

	// Windows wraps native menu bars when their labels exceed the width.
	// ImGui's stock main menu bar reserves one row; reserve all needed rows
	// here while preserving the same menu order and one menu navigation set.
	const char *labels[] = {"File", "View", "System", "Input", "Cheat", "Debug",
		"Record", "Tools", "Online Play", "Window", "Help"};
	const ImGuiStyle& style = ImGui::GetStyle();
	const float margin = std::max(style.DisplaySafeAreaPadding.x, style.WindowPadding.x);
	const float available = ImGui::GetMainViewport()->Size.x - margin * 2;
	const float rowHeight = ImGui::GetFrameHeight();
	auto menuWidth = [&](const char *label) {
		return ImGui::CalcTextSize(label).x + style.ItemSpacing.x * 2;
	};
	int rows = 1;
	float used = 0;
	for (const char *label : labels) {
		const float width = menuWidth(label);
		if (used > 0 && used + width > available) { ++rows; used = 0; }
		used += width;
	}
	const float height = rowHeight * rows;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_MenuBarBg]);
	const bool visible = ImGui::BeginViewportSideBar("##MainMenuBar",
		ImGui::GetMainViewport(), ImGuiDir_Up, height,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_MenuBar);
	ImGui::PopStyleColor();
	if (!visible) {
		ImGui::End();
		ATUIProcessDeferredMenuDialogs(window);
		return;
	}
	ImGuiWindow *menuWindow = ImGui::GetCurrentWindow();
	menuWindow->DC.MenuBarOffset.x = margin;
	menuWindow->MenuBarHeight = height;
	ImGui::BeginMenuBar();
	// BeginMenu positions popups relative to the current row, not the total
	// wrapped bar height. BeginMenuBar has already installed the full clip.
	menuWindow->MenuBarHeight = rowHeight;
	const ImVec2 start = ImGui::GetCursorScreenPos();
	int row = 0;
	used = 0;
	auto beginMenu = [&](const char *label) {
		const float width = menuWidth(label);
		if (used > 0 && used + width > available) {
			++row;
			used = 0;
			ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + row * rowHeight));
			menuWindow->DC.IsSameLine = false;
			menuWindow->DC.CurrLineSize = ImVec2(0, 0);
			ImGui::AlignTextToFramePadding();
		}
		used += width;
		return ImGui::BeginMenu(label);
	};

	if (beginMenu("File")) { RenderFileMenu(sim, state, window); ImGui::EndMenu(); }
	if (beginMenu("View")) { ATUIRenderViewMenu(sim, state, window, backend); ImGui::EndMenu(); }
	if (beginMenu("System")) { ATUIRenderSystemMenu(sim, state); ImGui::EndMenu(); }
	if (beginMenu("Input")) { RenderInputMenu(sim, state); ImGui::EndMenu(); }
	if (beginMenu("Cheat")) { RenderCheatMenu(sim, state); ImGui::EndMenu(); }
	if (beginMenu("Debug")) { ATUIRenderDebugMenu(sim); ImGui::EndMenu(); }
	if (beginMenu("Record")) { RenderRecordMenu(sim, window); ImGui::EndMenu(); }
	if (beginMenu("Tools")) { RenderToolsMenu(sim, state, window); ImGui::EndMenu(); }
	if (beginMenu("Online Play")) { ATUIRenderOnlineMenu(); ImGui::EndMenu(); }
	if (beginMenu("Window")) { RenderWindowMenu(window); ImGui::EndMenu(); }
	if (beginMenu("Help")) { RenderHelpMenu(state); ImGui::EndMenu(); }

	ATUIRenderSearchMenuInput();

	// Store the menu bar height so the display rect calculation can offset
	// the emulator screen below the menu bar.
	g_menuBarHeight = height;

	ImGui::EndMainMenuBar();
#endif

	ATUIProcessDeferredMenuDialogs(window);
}
