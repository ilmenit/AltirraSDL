//	AltirraSDL - Menu bar rendering
//	All menu functions (File, View, System, Input, Cheat, Debug, Record,
//	Tools, Window, Help) and their associated callbacks and state.

#include <stdafx.h>
#include <mutex>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "display_backend.h"

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
#include "settings.h"
#include "options.h"
#include "firmwaremanager.h"

#include <algorithm>
#include <string>
#include <vector>

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;
extern bool g_copyFrameRequested;  // defined in ui_main.cpp
extern SDL_Window *g_pWindow;      // defined in main_sdl3.cpp

// Right-click context menu for menu items — "Assign Keyboard Shortcut..."
// Call after ImGui::MenuItem() for items that have an accel command name.
static void ShortcutContextMenu(const char *command) {
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("Assign Keyboard Shortcut..."))
			ATUIOpenShortcutEditor(command);
		ImGui::EndPopup();
	}
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
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_BootImage, filelist[0]);
}

static void OpenImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_OpenImage, filelist[0]);
}

static void CartridgeAttachCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_AttachCartridge, filelist[0]);
}

// Per-drive attach callback — drive index in userdata
static void AttachDiskCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;
	ATUIPushDeferred(kATDeferred_AttachDisk, filelist[0], driveIdx);
}

static void CassetteSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
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

void ATUIQuickSaveState() {
	try {
		vdrefptr<IATSerializable> info;
		g_sim.CreateSnapshot(~g_pQuickSaveState, ~info);
		fprintf(stderr, "[AltirraSDL] Quick save state created\n");
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Quick save failed\n");
	}
}

void ATUIQuickLoadState() {
	if (!g_pQuickSaveState)
		return;
	try {
		g_sim.ApplySnapshot(*g_pQuickSaveState, nullptr);
		g_sim.Resume();
		fprintf(stderr, "[AltirraSDL] Quick load state applied\n");
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Quick load failed\n");
	}
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
	SDL_ShowOpenFileDialog(BootImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);
}

void ATUIShowOpenImageDialog(SDL_Window *window) {
	SDL_ShowOpenFileDialog(OpenImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);
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
	SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
		if (fl && fl[0]) {
			std::lock_guard<std::mutex> lock(g_pendingSourceMutex);
			g_pendingSourcePath = fl[0];
		}
	}, nullptr, window, srcFilters, 2, nullptr, false);
}

void ATUIShowSaveFrameDialog(SDL_Window *window) {
	static const SDL_DialogFileFilter filters[] = {
		{ "BMP Images", "bmp" },
	};
	SDL_ShowSaveFileDialog(ATUISaveFrameCallback, nullptr, window, filters, 1, nullptr);
}

// =========================================================================
// Paste Text — port of Windows main.cpp Paste() function
// =========================================================================

// Scancode name table for {token} parsing in paste text — matches Windows
// main.cpp g_scancodeNameTable exactly.
static const struct { uint8 scancode; const wchar_t *name; } kScancodeNames[] = {
	{ 0x2C, L"tab" },
	{ 0x34, L"back" },
	{ 0x34, L"backspace" },
	{ 0x34, L"bksp" },
	{ 0x0C, L"enter" },
	{ 0x0C, L"return" },
	{ 0x1C, L"esc" },
	{ 0x1C, L"escape" },
	{ 0x27, L"fuji" },
	{ 0x27, L"inv" },
	{ 0x27, L"invert" },
	{ 0x11, L"help" },
	{ 0x76, L"clear" },
	{ 0xB4, L"del" },
	{ 0xB4, L"delete" },
	{ 0xB7, L"ins" },
	{ 0xB7, L"insert" },
	{ 0x3C, L"caps" },
	{ 0x86, L"left" },
	{ 0x87, L"right" },
	{ 0x8E, L"up" },
	{ 0x8F, L"down" },
};

// Case-insensitive wchar_t comparison for scancode name lookup.
static bool WcsiEqual(const wchar_t *a, size_t alen, const wchar_t *b) {
	size_t blen = wcslen(b);
	if (alen != blen) return false;
	for (size_t i = 0; i < alen; ++i) {
		wchar_t ca = a[i], cb = b[i];
		if (ca >= L'A' && ca <= L'Z') ca += L'a' - L'A';
		if (cb >= L'A' && cb <= L'Z') cb += L'a' - L'A';
		if (ca != cb) return false;
	}
	return true;
}

void ATUIPasteTextDirect(const wchar_t *text, size_t textLen) {
	auto& pokey = g_sim.GetPokey();
	const wchar_t *s = text;
	size_t len = textLen;
	vdfastvector<wchar_t> pasteChars;

	while (len--) {
		wchar_t c = *s++;
		if (!c) continue;

		int repeat = 1;
		switch (c) {
			case L'\u200B': case L'\u200C': case L'\u200D':
			case L'\u200E': case L'\u200F': continue;
			case L'\u2010': case L'\u2011': case L'\u2012':
			case L'\u2013': case L'\u2014': case L'\u2015': c = L'-'; break;
			case L'\u2018': case L'\u2019': c = L'\''; break;
			case L'\u201C': case L'\u201D': c = L'"'; break;
			case L'\u2026': c = L'.'; repeat = 3; break;
			case L'\uFEFF': continue;
		}
		while (repeat--) pasteChars.push_back(c);
	}

	pasteChars.push_back(0);
	wchar_t skipLT = 0;
	uint8 scancodeModifier = 0;
	const wchar_t *t = pasteChars.data();

	while (wchar_t c = *t++) {
		if (c == skipLT) { skipLT = 0; continue; }
		skipLT = 0;

		// {token} parsing — matches Windows main.cpp Paste() exactly.
		// Supports: {enter}, {tab}, {esc}, {left}, {right}, {up}, {down},
		// {fuji}, {help}, {clear}, {del}, {ins}, {caps}, {back},
		// and modifier prefixes: {shift-X}, {ctrl-X}, {+X}, {^X}.
		if (c == L'{') {
			const wchar_t *start = t;
			while (*t && *t != L'}')
				++t;
			if (*t != L'}')
				break;

			const wchar_t *nameStart = start;
			size_t nameLen = (size_t)(t - start);
			++t; // skip '}'

			// Parse modifier prefixes
			while (nameLen > 0) {
				if (nameStart[0] == L'+') {
					scancodeModifier |= 0x40;
					++nameStart; --nameLen;
					continue;
				}
				if (nameLen >= 6 && (WcsiEqual(nameStart, 6, L"shift-") || WcsiEqual(nameStart, 6, L"shift+"))) {
					scancodeModifier |= 0x40;
					nameStart += 6; nameLen -= 6;
					continue;
				}
				if (nameStart[0] == L'^') {
					scancodeModifier |= 0x80;
					++nameStart; --nameLen;
					continue;
				}
				if (nameLen >= 5 && (WcsiEqual(nameStart, 5, L"ctrl-") || WcsiEqual(nameStart, 5, L"ctrl+"))) {
					scancodeModifier |= 0x80;
					nameStart += 5; nameLen -= 5;
					continue;
				}
				break;
			}

			// Look up scancode name
			bool found = false;
			for (const auto& entry : kScancodeNames) {
				if (WcsiEqual(nameStart, nameLen, entry.name)) {
					uint8 scancode = entry.scancode;
					if (scancodeModifier)
						scancode = (scancode & 0x3F) | scancodeModifier;
					pokey.PushKey(scancode, false, true, false, true);
					found = true;
					break;
				}
			}

			scancodeModifier = 0;
			continue;
		}

		const uint8 kInvalidScancode = 0xFF;
		uint8 scancode = kInvalidScancode;

		switch (c) {
			case L'\r': case L'\n':
				skipLT = c ^ (L'\r' ^ L'\n');
				scancode = 0x0C; break;
			case L'\t': scancode = 0x2C; break;
			case L'\x001B': scancode = 0x1C; break;
			default:
				if (ATUIGetDefaultScanCodeForCharacter(c, scancode)) {
					// For control characters, inject an ESC prefix so they
					// display as visible characters — matches Windows exactly.
					switch (scancode) {
						case 0x1C: case 0x8E: case 0x8F: case 0x86:
						case 0x87: case 0x82: case 0x76: case 0x34: case 0x2C:
							pokey.PushKey(0x1C, false, true, false, true);
							break;
					}
				} else scancode = kInvalidScancode;
				break;
		}

		if (scancode != kInvalidScancode)
			pokey.PushKey(scancode | scancodeModifier, false, true, false, true);

		scancodeModifier = 0;
	}
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
		SDL_ShowOpenFileDialog(BootImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);
	ShortcutContextMenu("File.BootImage");

	if (ImGui::MenuItem("Open Image...", ATUIGetShortcutStringForCommand("File.OpenImage")))
		ATUIShowOpenImageDialog(window);
	ShortcutContextMenu("File.OpenImage");

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
					SDL_ShowOpenFileDialog(AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, nullptr, false);
			} else {
				if (ImGui::MenuItem(label))
					SDL_ShowOpenFileDialog(AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, nullptr, false);
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
			SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_LoadCassette, fl[0]);
			}, nullptr, window, casFilters, 2, nullptr, false);
		}

		if (ImGui::MenuItem("Unload", nullptr, false, loaded))
			cas.Unload();

		if (ImGui::MenuItem("Save...", nullptr, false, loaded)) {
			SDL_ShowSaveFileDialog(CassetteSaveCallback, nullptr, window,
				kCasSaveFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Export Audio Tape...", nullptr, false, loaded)) {
			static const SDL_DialogFileFilter wavFilters[] = {
				{ "WAV Audio", "wav" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_ExportCassetteAudio, fl[0]);
			}, nullptr, window, wavFilters, 1, nullptr);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// State save/load
	{
		static const SDL_DialogFileFilter stateFilters[] = {
			{ "Save States", "atstate2;atstate" }, { "All Files", "*" },
		};

		if (ImGui::MenuItem("Load State..."))
			SDL_ShowOpenFileDialog(LoadStateCallback, nullptr, window, stateFilters, 2, nullptr, false);

		if (ImGui::MenuItem("Save State..."))
			SDL_ShowSaveFileDialog(SaveStateCallback, nullptr, window, stateFilters, 1, nullptr);

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
			SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_AttachSecondaryCartridge, fl[0]);
			}, nullptr, window, kCartFilters, 2, nullptr, false);

		if (ImGui::MenuItem("Detach", nullptr, false, sim.IsCartridgeAttached(1))) {
			sim.UnloadCartridge(1);
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				sim.ColdReset();
			sim.Resume();
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Attach Cartridge..."))
		SDL_ShowOpenFileDialog(CartridgeAttachCallback, nullptr, window, kCartFilters, 2, nullptr, false);

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
			SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveCartridge, fl[0]);
			}, nullptr, window, cartSaveFilters, 3, nullptr);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE / SIDE / MyIDE II Main Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)kATStorageId_Firmware))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)0, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE SDX Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 1)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)1, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save Ultimate1MB Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 2)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)2, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save Rapidus Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 3)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)3, window, fwFilters, 2, nullptr);
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
// View menu
// =========================================================================

static void RenderViewMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window, IDisplayBackend *backend) {
	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (ImGui::MenuItem("Full Screen", ATUIGetShortcutStringForCommand("View.ToggleFullScreen"), isFullscreen))
		ATSetFullscreen(!isFullscreen);

	ImGui::Separator();

	if (ImGui::BeginMenu("Filter Mode")) {
		ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
		if (ImGui::MenuItem("Next Mode")) {
			static const ATDisplayFilterMode kModes[] = {
				kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
				kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
				kATDisplayFilterMode_AnySuitable,
			};
			int cur = 0;
			for (int i = 0; i < 5; ++i)
				if (kModes[i] == fm) { cur = i; break; }
			ATUISetDisplayFilterMode(kModes[(cur + 1) % 5]);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Point", nullptr, fm == kATDisplayFilterMode_Point))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Point);
		if (ImGui::MenuItem("Bilinear", nullptr, fm == kATDisplayFilterMode_Bilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
		if (ImGui::MenuItem("Sharp Bilinear", nullptr, fm == kATDisplayFilterMode_SharpBilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
		if (ImGui::MenuItem("Bicubic", nullptr, fm == kATDisplayFilterMode_Bicubic))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic);
		if (ImGui::MenuItem("Default (Any Suitable)", nullptr, fm == kATDisplayFilterMode_AnySuitable))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Filter Sharpness")) {
		int sharpness = ATUIGetViewFilterSharpness();
		if (ImGui::MenuItem("Softer", nullptr, sharpness == -2))
			ATUISetViewFilterSharpness(-2);
		if (ImGui::MenuItem("Soft", nullptr, sharpness == -1))
			ATUISetViewFilterSharpness(-1);
		if (ImGui::MenuItem("Normal", nullptr, sharpness == 0))
			ATUISetViewFilterSharpness(0);
		if (ImGui::MenuItem("Sharp", nullptr, sharpness == 1))
			ATUISetViewFilterSharpness(1);
		if (ImGui::MenuItem("Sharper", nullptr, sharpness == 2))
			ATUISetViewFilterSharpness(2);
		ImGui::EndMenu();
	}

	// Video Frame submenu (matches Windows "Video Frame" menu)
	if (ImGui::BeginMenu("Video Frame")) {
		ATDisplayStretchMode sm = ATUIGetDisplayStretchMode();
		if (ImGui::MenuItem("Fit to Window", nullptr, sm == kATDisplayStretchMode_Unconstrained))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained);
		if (ImGui::MenuItem("Preserve Aspect Ratio", nullptr, sm == kATDisplayStretchMode_PreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio);
		if (ImGui::MenuItem("Preserve Aspect Ratio (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_IntegralPreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio);
		if (ImGui::MenuItem("Square Pixels", nullptr, sm == kATDisplayStretchMode_SquarePixels))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels);
		if (ImGui::MenuItem("Square Pixels (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_Integral))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral);

		ImGui::Separator();

		{
			bool pzActive = ATUIIsPanZoomToolActive();
			if (ImGui::MenuItem("Pan/Zoom Tool", nullptr, pzActive))
				ATUISetPanZoomToolActive(!pzActive);
		}
		if (ImGui::MenuItem("Reset Pan and Zoom")) {
			ATUISetDisplayZoom(1.0f);
			ATUISetDisplayPanOffset({0, 0});
		}
		if (ImGui::MenuItem("Reset Panning"))
			ATUISetDisplayPanOffset({0, 0});
		if (ImGui::MenuItem("Reset Zoom"))
			ATUISetDisplayZoom(1.0f);

		ImGui::EndMenu();
	}

	// Overscan Mode submenu (matches Windows exactly, with sub-submenus)
	if (ImGui::BeginMenu("Overscan Mode")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto om = gtia.GetOverscanMode();
		if (ImGui::MenuItem("OS Screen Only", nullptr, om == ATGTIAEmulator::kOverscanOSScreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
		if (ImGui::MenuItem("Normal", nullptr, om == ATGTIAEmulator::kOverscanNormal))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
		if (ImGui::MenuItem("Widescreen", nullptr, om == ATGTIAEmulator::kOverscanWidescreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanWidescreen);
		if (ImGui::MenuItem("Extended", nullptr, om == ATGTIAEmulator::kOverscanExtended))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
		if (ImGui::MenuItem("Full (With Blanking)", nullptr, om == ATGTIAEmulator::kOverscanFull))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanFull);

		ImGui::Separator();

		// Vertical Override sub-submenu
		if (ImGui::BeginMenu("Vertical Override")) {
			auto vom = gtia.GetVerticalOverscanMode();
			if (ImGui::MenuItem("Off", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Default))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Default);
			if (ImGui::MenuItem("OS Screen Only", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_OSScreen))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_OSScreen);
			if (ImGui::MenuItem("Normal", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Normal))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Normal);
			if (ImGui::MenuItem("Extended", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Extended))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Extended);
			if (ImGui::MenuItem("Full (With Blanking)", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Full))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Full);
			ImGui::EndMenu();
		}

		bool palExt = gtia.IsOverscanPALExtended();
		if (ImGui::MenuItem("Extended PAL Height", nullptr, palExt))
			gtia.SetOverscanPALExtended(!palExt);

		bool indicatorMargin = ATUIGetDisplayPadIndicators();
		if (ImGui::MenuItem("Indicator Margin", nullptr, indicatorMargin))
			ATUISetDisplayPadIndicators(!indicatorMargin);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	// VSync toggle — works with both GL and SDL_Renderer backends
	{
		bool vsyncOn = false;
		if (backend->GetType() == DisplayBackendType::OpenGL33) {
			int interval = 0;
			SDL_GL_GetSwapInterval(&interval);
			vsyncOn = (interval != 0);
			if (ImGui::MenuItem("Vertical Sync", nullptr, vsyncOn))
				SDL_GL_SetSwapInterval(vsyncOn ? 0 : 1);
		} else {
			int vsync = 0;
			SDL_GetRenderVSync(backend->GetSDLRenderer(), &vsync);
			vsyncOn = (vsync != 0);
			if (ImGui::MenuItem("Vertical Sync", nullptr, vsyncOn))
				SDL_SetRenderVSync(backend->GetSDLRenderer(), vsyncOn ? 0 : 1);
		}
	}

	bool showFPS = ATUIGetShowFPS();
	if (ImGui::MenuItem("Show FPS", nullptr, showFPS))
		ATUISetShowFPS(!showFPS);

	// Video Outputs submenu
	if (ImGui::BeginMenu("Video Outputs")) {
		bool altView = ATUIGetAltViewEnabled();
		if (ImGui::MenuItem("1 Computer Output", nullptr, !altView))
			ATUISetAltViewEnabled(false);

		if (ATUIIsAltOutputAvailable()) {
			if (ImGui::MenuItem("Next Output"))
				ATUISelectNextAltOutput();
		}

		bool autoSwitch = ATUIGetAltViewAutoswitchingEnabled();
		if (ImGui::MenuItem("Auto-Switch Video Output", nullptr, autoSwitch))
			ATUISetAltViewAutoswitchingEnabled(!autoSwitch);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Adjust Colors..."))
		state.showAdjustColors = true;

	// Screen Effects submenu — combines built-in effects (Basic) and
	// librashader presets into a unified mode selector.
	if (ImGui::BeginMenu("Screen Effects")) {
		IDisplayBackend *be = ATUIGetDisplayBackend();
		bool shaderAvail = be && be->SupportsExternalShaders();
		bool hasPreset = be && be->HasShaderPreset();

		// Sync mode from actual backend state
		if (hasPreset)
			state.screenEffectsMode = ATUIState::kSFXMode_Preset;

		bool isNone = (state.screenEffectsMode == ATUIState::kSFXMode_None);
		bool isBasic = (state.screenEffectsMode == ATUIState::kSFXMode_Basic);
		bool isPreset = (state.screenEffectsMode == ATUIState::kSFXMode_Preset);

		// (None) — disable all effects
		if (ImGui::MenuItem("(None)", nullptr, isNone)) {
			if (be) be->ClearShaderPreset();
			state.screenEffectsMode = ATUIState::kSFXMode_None;
		}

		// Basic — built-in Altirra effects (scanlines, bloom, distortion, mask)
		if (ImGui::MenuItem("Basic", nullptr, isBasic)) {
			if (be) be->ClearShaderPreset();
			state.screenEffectsMode = ATUIState::kSFXMode_Basic;
		}

		ImGui::Separator();

		// Preset submenu — shader directory tree + Browse
		ATUIRenderShaderPresetMenu(backend);
		if (!shaderAvail && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("librashader not found.\nUse Shader Setup or rebuild with --librashader.");

		ImGui::Separator();

		// Shader Parameters — context-sensitive
		{
			bool canShowParams = !isNone;
			if (ImGui::MenuItem("Shader Parameters...", nullptr, false, canShowParams)) {
				if (isPreset)
					state.showShaderParams = true;
				else
					state.showScreenEffects = true;
			}
			if (!canShowParams && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Select Basic or a Preset first.");
		}

		// Shader Setup — always accessible
		if (ImGui::MenuItem("Shader Setup..."))
			state.showShaderSetup = true;

		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Customize HUD..."))
		state.showCustomizeHud = true;
	if (ImGui::MenuItem("Calibrate..."))
		state.showCalibrate = true;

	ImGui::Separator();

	{
		bool dbgOpen = ATUIDebuggerIsOpen();
		if (ImGui::MenuItem("Display", nullptr, false, dbgOpen))
			ATUIDebuggerFocusDisplay();
		if (ImGui::MenuItem("Printer Output", nullptr, false, dbgOpen))
			ATActivateUIPane(kATUIPaneId_PrinterOutput, true, true);
	}

	ImGui::Separator();

	// Copy/Save Frame
	if (ImGui::MenuItem("Copy Frame to Clipboard", ATUIGetShortcutStringForCommand("Edit.CopyFrame")))
		g_copyFrameRequested = true;
	ImGui::MenuItem("Copy Frame to Clipboard (True Aspect)", nullptr, false, false);  // placeholder

	if (ImGui::MenuItem("Save Frame...", ATUIGetShortcutStringForCommand("Edit.SaveFrame")))
		ATUIShowSaveFrameDialog(window);
	ImGui::MenuItem("Save Frame (True Aspect)...", nullptr, false, false);  // placeholder

	// Text Selection submenu
	if (ImGui::BeginMenu("Text Selection")) {
		bool hasSelection = ATUIIsTextSelected();
		if (ImGui::MenuItem("Copy Text", ATUIGetShortcutStringForCommand("Edit.CopyText"), false, hasSelection))
			ATUITextCopy(ATTextCopyMode::ASCII);
		if (ImGui::MenuItem("Copy Escaped Text", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Escaped);
		if (ImGui::MenuItem("Copy Hex", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Hex);
		if (ImGui::MenuItem("Copy Unicode", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Unicode);
		if (ImGui::MenuItem("Paste Text", ATUIGetShortcutStringForCommand("Edit.PasteText")))
			ATUIPasteText();
		ImGui::Separator();
		if (ImGui::MenuItem("Select All", ATUIGetShortcutStringForCommand("Edit.SelectAll")))
			ATUITextSelectAll();
		if (ImGui::MenuItem("Deselect", ATUIGetShortcutStringForCommand("Edit.Deselect"), false, hasSelection))
			ATUITextDeselect();
		ImGui::EndMenu();
	}
}

// =========================================================================
// System menu
// =========================================================================

static void RenderSystemMenu(ATSimulator &sim, ATUIState &state) {
	// Profiles submenu (matches Windows System > Profiles)
	if (ImGui::BeginMenu("Profiles")) {
		if (ImGui::MenuItem("Edit Profiles..."))
			state.showProfiles = true;

		bool temporary = ATSettingsGetTemporaryProfileMode();
		if (ImGui::MenuItem("Temporary Profile", nullptr, temporary))
			ATSettingsSetTemporaryProfileMode(!temporary);

		ImGui::Separator();

		// Quick profile switching — list visible profiles
		uint32 currentId = ATSettingsGetCurrentProfileId();

		// Global profile
		{
			VDStringW name = ATSettingsProfileGetName(0);
			VDStringA nameU8 = VDTextWToU8(name);
			if (ImGui::MenuItem(nameU8.c_str(), nullptr, currentId == 0)) {
				if (currentId != 0) {
					ATSettingsSwitchProfile(0);
					sim.Resume();
				}
			}
		}

		// Enumerated profiles (visible only)
		vdfastvector<uint32> profileIds;
		ATSettingsProfileEnum(profileIds);
		for (uint32 id : profileIds) {
			if (!ATSettingsProfileGetVisible(id))
				continue;
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA nameU8 = VDTextWToU8(name);
			if (ImGui::MenuItem(nameU8.c_str(), nullptr, currentId == id)) {
				if (currentId != id) {
					ATSettingsSwitchProfile(id);
					sim.Resume();
				}
			}
		}

		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Configure System...", ATUIGetShortcutStringForCommand("System.Configure")))
		state.showSystemConfig = true;
	ShortcutContextMenu("System.Configure");

	ImGui::Separator();

	if (ImGui::MenuItem("Warm Reset", ATUIGetShortcutStringForCommand("System.WarmReset"))) {
		sim.WarmReset();
		sim.Resume();
	}
	ShortcutContextMenu("System.WarmReset");
	if (ImGui::MenuItem("Cold Reset", ATUIGetShortcutStringForCommand("System.ColdReset"))) {
		sim.ColdReset();
		sim.Resume();
		if (!g_kbdOpts.mbAllowShiftOnColdReset)
			sim.GetPokey().SetShiftKeyState(false, true);
	}
	if (ImGui::MenuItem("Cold Reset (Computer Only)")) {
		sim.ColdResetComputerOnly();
		sim.Resume();
		if (!g_kbdOpts.mbAllowShiftOnColdReset)
			sim.GetPokey().SetShiftKeyState(false, true);
	}

	ShortcutContextMenu("System.ColdReset");
	bool paused = sim.IsPaused();
	if (ImGui::MenuItem("Pause", ATUIGetShortcutStringForCommand("System.TogglePause"), paused)) {
		if (paused) sim.Resume(); else sim.Pause();
	}
	ShortcutContextMenu("System.TogglePause");

	ImGui::Separator();

	bool turbo = ATUIGetTurbo();
	if (ImGui::MenuItem("Warp Speed", nullptr, turbo))
		ATUISetTurbo(!turbo);

	bool pauseInactive = ATUIGetPauseWhenInactive();
	if (ImGui::MenuItem("Pause When Inactive", nullptr, pauseInactive))
		ATUISetPauseWhenInactive(!pauseInactive);

	// Rewind submenu
	if (ImGui::BeginMenu("Rewind")) {
		IATAutoSaveManager &mgr = sim.GetAutoSaveManager();
		bool rewindEnabled = mgr.GetRewindEnabled();

		if (ImGui::MenuItem("Quick Rewind", nullptr, false, rewindEnabled))
			ATUIQuickRewind();
		if (ImGui::MenuItem("Rewind...", nullptr, false, rewindEnabled)) {
			if (ATUIOpenRewindDialog())
				state.showRewind = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Enable Rewind Recording", nullptr, rewindEnabled))
			mgr.SetRewindEnabled(!rewindEnabled);
		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("Power-On Delay")) {
		int delay = sim.GetPowerOnDelay();
		if (ImGui::MenuItem("Auto", nullptr, delay < 0))
			sim.SetPowerOnDelay(-1);
		if (ImGui::MenuItem("None", nullptr, delay == 0))
			sim.SetPowerOnDelay(0);
		if (ImGui::MenuItem("1 Second", nullptr, delay == 10))
			sim.SetPowerOnDelay(10);
		if (ImGui::MenuItem("2 Seconds", nullptr, delay == 20))
			sim.SetPowerOnDelay(20);
		if (ImGui::MenuItem("3 Seconds", nullptr, delay == 30))
			sim.SetPowerOnDelay(30);
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Hold Keys For Reset"))
		ATUIToggleHoldKeys();

	bool basic = sim.IsBASICEnabled();
	if (ImGui::MenuItem("Internal BASIC (Boot Without Option Key)", nullptr, basic)) {
		sim.SetBASICEnabled(!basic);
		if (ATUIIsResetNeeded(kATUIResetFlag_BasicChange))
			sim.ColdReset();
	}

	bool casAutoBoot = sim.IsCassetteAutoBootEnabled();
	if (ImGui::MenuItem("Auto-Boot Tape (Hold Start)", nullptr, casAutoBoot))
		sim.SetCassetteAutoBootEnabled(!casAutoBoot);

	ImGui::Separator();

	// Console Switches submenu (matches Windows menu_default.txt exactly)
	if (ImGui::BeginMenu("Console Switches")) {
		bool kbdPresent = sim.IsKeyboardPresent();
		if (ImGui::MenuItem("Keyboard Present (XEGS)", nullptr, kbdPresent))
			sim.SetKeyboardPresent(!kbdPresent);

		bool selfTest = sim.IsForcedSelfTest();
		if (ImGui::MenuItem("Force Self-Test", nullptr, selfTest))
			sim.SetForcedSelfTest(!selfTest);

		// "Activate Cart Menu Button" — momentary action (matches Windows Cart.ActivateMenuButton)
		if (ImGui::MenuItem("Activate Cart Menu Button"))
			ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true);

		// "Enable Cart Switch" — toggle (matches Windows Cart.ToggleSwitch)
		bool cartSwitch = sim.GetCartridgeSwitch();
		if (ImGui::MenuItem("Enable Cart Switch", nullptr, cartSwitch))
			sim.SetCartridgeSwitch(!cartSwitch);

		// Device buttons (shown only when the device is present)
		static const struct { ATDeviceButton btn; const char *label; } kDevButtons[] = {
			{ kATDeviceButton_BlackBoxDumpScreen, "BlackBox: Dump Screen" },
			{ kATDeviceButton_BlackBoxMenu, "BlackBox: Menu" },
			{ kATDeviceButton_IDEPlus2SwitchDisks, "IDE Plus 2.0: Switch Disks" },
			{ kATDeviceButton_IDEPlus2WriteProtect, "IDE Plus 2.0: Write Protect" },
			{ kATDeviceButton_IDEPlus2SDX, "IDE Plus 2.0: SDX Enable" },
			{ kATDeviceButton_IndusGTError, "Indus GT: Error Button" },
			{ kATDeviceButton_IndusGTTrack, "Indus GT: Track Button" },
			{ kATDeviceButton_IndusGTId, "Indus GT: Drive Type Button" },
			{ kATDeviceButton_IndusGTBootCPM, "Indus GT: Boot CP/M" },
			{ kATDeviceButton_IndusGTChangeDensity, "Indus GT: Change Density" },
			{ kATDeviceButton_HappySlow, "Happy: Slow Switch" },
			{ kATDeviceButton_HappyWPEnable, "Happy 1050: Write protect disk" },
			{ kATDeviceButton_HappyWPDisable, "Happy 1050: Write enable disk" },
			{ kATDeviceButton_ATR8000Reset, "ATR8000: Reset" },
			{ kATDeviceButton_XELCFSwap, "XEL-CF3: Swap" },
		};

		bool anyDevBtn = false;
		for (auto& db : kDevButtons) {
			if (ATUIGetDeviceButtonSupported((uint32)db.btn)) {
				if (!anyDevBtn) {
					ImGui::Separator();
					anyDevBtn = true;
				}
				bool dep = ATUIGetDeviceButtonDepressed((uint32)db.btn);
				if (ImGui::MenuItem(db.label, nullptr, dep))
					ATUIActivateDeviceButton((uint32)db.btn, !dep);
			}
		}
		ImGui::EndMenu();
	}

}

// =========================================================================
// Input menu
// =========================================================================

// Render a single Port submenu.  Queries ATInputManager for all input maps
// that touch the given physical port, presents them as radio items, and
// toggles activation.  Mirrors Windows uiportmenus.cpp behavior exactly.
static void RenderPortSubmenu(ATInputManager &im, int portIdx) {
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

	if (ImGui::MenuItem("None", nullptr, !anyActive)) {
		// Deactivate all maps for this port
		for (const auto &e : entries)
			if (e.active)
				im.ActivateInputMap(e.map, false);
	}

	// One radio item per input map — strict radio behavior matching Windows:
	// clicking any item activates it and deactivates all others for this port.
	// Clicking the already-active item is a no-op (it stays selected).
	for (const auto &e : entries) {
		if (ImGui::MenuItem(e.name.c_str(), nullptr, e.active)) {
			for (const auto &other : entries)
				im.ActivateInputMap(other.map, &other == &e);
		}
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
			if (pMap) {
				VDStringA msg;
				msg = "Quick map: ";
				msg += VDTextWToU8(pMap->GetName(), -1);
				fprintf(stderr, "[AltirraSDL] %s\n", msg.c_str());
			} else {
				fprintf(stderr, "[AltirraSDL] Quick maps disabled\n");
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
// Debug menu
// =========================================================================

static void RenderDebugMenu(ATSimulator &sim) {
	IATDebugger *dbg = ATGetDebugger();
	bool dbgEnabled = ATUIDebuggerIsOpen();
	bool dbgRunning = dbg && dbg->IsRunning();

	if (ImGui::MenuItem("Enable Debugger", nullptr, dbgEnabled)) {
		if (dbgEnabled)
			ATUIDebuggerClose();
		else
			ATUIDebuggerOpen();
	}
	if (ImGui::MenuItem("Open Source File...", ATUIGetShortcutStringForCommand("Debug.OpenSourceFile"), false, dbgEnabled))
		ATUIShowOpenSourceFileDialog(g_pWindow);
	if (ImGui::MenuItem("Source File List...", nullptr, false, dbgEnabled))
		ATUIDebuggerShowSourceListDialog();

	if (ImGui::BeginMenu("Window", dbgEnabled)) {
		if (ImGui::MenuItem("Console"))
			ATActivateUIPane(kATUIPaneId_Console, true);
		if (ImGui::MenuItem("Registers"))
			ATActivateUIPane(kATUIPaneId_Registers, true);
		if (ImGui::MenuItem("Disassembly"))
			ATActivateUIPane(kATUIPaneId_Disassembly, true);
		if (ImGui::MenuItem("Call Stack"))
			ATActivateUIPane(kATUIPaneId_CallStack, true);
		if (ImGui::MenuItem("History"))
			ATActivateUIPane(kATUIPaneId_History, true);
		if (ImGui::BeginMenu("Memory")) {
			for (int i = 0; i < 4; ++i) {
				char label[16];
				snprintf(label, sizeof(label), "Memory %d", i + 1);
				if (ImGui::MenuItem(label))
					ATActivateUIPane(kATUIPaneId_MemoryN + i, true);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Watch")) {
			for (int i = 0; i < 4; ++i) {
				char label[16];
				snprintf(label, sizeof(label), "Watch %d", i + 1);
				if (ImGui::MenuItem(label))
					ATActivateUIPane(kATUIPaneId_WatchN + i, true);
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Breakpoints"))
			ATActivateUIPane(kATUIPaneId_Breakpoints, true);
		ImGui::MenuItem("Targets", nullptr, false, false);
		ImGui::MenuItem("Debug Display", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Visualization")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto am = gtia.GetAnalysisMode();

		if (ImGui::MenuItem("Cycle GTIA Visualization")) {
			int next = ((int)am + 1) % ATGTIAEmulator::kAnalyzeCount;
			gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)next);
		}
		if (ImGui::MenuItem("Cycle ANTIC Visualization")) {
			ATAnticEmulator& antic = sim.GetAntic();
			int next = ((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount;
			antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)next);
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Options")) {
		if (dbg) {
			bool breakAtExe = dbg->IsBreakOnEXERunAddrEnabled();
			if (ImGui::MenuItem("Break at EXE Run Address", nullptr, breakAtExe))
				dbg->SetBreakOnEXERunAddrEnabled(!breakAtExe);
		}
		{
			bool autoReload = sim.IsROMAutoReloadEnabled();
			if (ImGui::MenuItem("Auto-Reload ROMs on Cold Reset", nullptr, autoReload))
				sim.SetROMAutoReloadEnabled(!autoReload);
		}
		{
			bool randomEXE = sim.IsRandomFillEXEEnabled();
			if (ImGui::MenuItem("Randomize Memory On EXE Load", nullptr, randomEXE))
				sim.SetRandomFillEXEEnabled(!randomEXE);
		}
		ImGui::Separator();
		ImGui::MenuItem("Change Font...", nullptr, false, false);                   // TODO
		ImGui::EndMenu();
	}

	ImGui::Separator();

	{
		// Run/Break shows combined hint from Debug.Run (F5) and Debug.RunStop (F8)
		const char *runKey = ATUIGetShortcutStringForCommand("Debug.Run");
		const char *stopKey = ATUIGetShortcutStringForCommand("Debug.RunStop");
		static char runBreakHint[64];
		if (runKey[0] && stopKey[0])
			snprintf(runBreakHint, sizeof(runBreakHint), "%s/%s", runKey, stopKey);
		else if (runKey[0])
			snprintf(runBreakHint, sizeof(runBreakHint), "%s", runKey);
		else
			snprintf(runBreakHint, sizeof(runBreakHint), "%s", stopKey);
		if (ImGui::MenuItem("Run/Break", runBreakHint, false, dbgEnabled))
			ATUIDebuggerRunStop();
	}
	if (ImGui::MenuItem("Break", nullptr, false, dbgEnabled && dbgRunning))
		ATUIDebuggerBreak();

	ImGui::Separator();

	if (ImGui::MenuItem("Step Into", ATUIGetShortcutStringForCommand("Debug.StepInto"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerStepInto();
	if (ImGui::MenuItem("Step Over", ATUIGetShortcutStringForCommand("Debug.StepOver"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerStepOver();
	if (ImGui::MenuItem("Step Out", ATUIGetShortcutStringForCommand("Debug.StepOut"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerStepOut();
	if (ImGui::MenuItem("Toggle Breakpoint", ATUIGetShortcutStringForCommand("Debug.ToggleBreakpoint"), false, dbgEnabled && !dbgRunning))
		ATUIDebuggerToggleBreakpoint();
	if (ImGui::MenuItem("New Breakpoint...", ATUIGetShortcutStringForCommand("Debug.NewBreakpoint"), false, dbgEnabled)) {
		ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);
		ATUIDebuggerShowBreakpointDialog(-1);
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("Profile")) {
		if (ImGui::MenuItem("Profile View"))
			ATActivateUIPane(kATUIPaneId_Profiler, true);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Verifier...", nullptr, g_sim.IsVerifierEnabled()))
		ATUIShowDialogVerifier();
	if (ImGui::MenuItem("Performance Analyzer..."))
		ATActivateUIPane(kATUIPaneId_Profiler, true);
}

// =========================================================================
// Record menu
// =========================================================================

static void RenderRecordMenu(ATSimulator &sim, SDL_Window *window) {
	bool recording = ATUIIsRecording();

	if (ImGui::MenuItem("Record Raw Audio...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter rawFilters[] = {
			{ "Raw PCM Audio", "pcm" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordRaw, fl[0]);
		}, nullptr, window, rawFilters, 1, nullptr);
	}

	if (ImGui::MenuItem("Record Audio...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter wavFilters[] = {
			{ "WAV Audio", "wav" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordWAV, fl[0]);
		}, nullptr, window, wavFilters, 1, nullptr);
	}

	if (ImGui::MenuItem("Record Video...", nullptr, ATUIIsVideoRecording(), !recording))
		ATUIShowVideoRecordingDialog();

	if (ImGui::MenuItem("Record SAP Type R...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter sapFilters[] = {
			{ "SAP Files", "sap" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordSAP, fl[0]);
		}, nullptr, window, sapFilters, 1, nullptr);
	}

	if (ImGui::MenuItem("Record VGM...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter vgmFilters[] = {
			{ "VGM Audio", "vgm" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordVGM, fl[0]);
		}, nullptr, window, vgmFilters, 1, nullptr);
	}

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
	if (ImGui::MenuItem("Disk Explorer..."))
		state.showDiskExplorer = true;

	if (ImGui::MenuItem("Convert SAP to EXE...")) {
		static const SDL_DialogFileFilter kSAPFilters[] = {
			{ "SAP Music Files", "sap" },
			{ "All Files", "*" },
		};
		SDL_ShowOpenFileDialog(SAPOpenCallback, window, window, kSAPFilters, 2, nullptr, false);
	}

	if (ImGui::MenuItem("Export ROM set..."))
		SDL_ShowOpenFolderDialog(ExportROMSetCallback, nullptr, window, nullptr, false);

	if (ImGui::MenuItem("Analyze tape decoding...")) {
		static const SDL_DialogFileFilter kTapeFilters[] = {
			{ "Audio Files", "wav;flac" },
			{ "All Files", "*" },
		};
		SDL_ShowOpenFileDialog(TapeAnalysisOpenCallback, window, window, kTapeFilters, 2, nullptr, false);
	}

	ImGui::Separator();

	if (ImGui::MenuItem("First Time Setup..."))
		state.showSetupWizard = true;

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
	ImGui::MenuItem("Contents", nullptr, false, false);  // placeholder — needs help system

	if (ImGui::MenuItem("About"))
		state.showAboutDialog = true;

	if (ImGui::MenuItem("Change Log"))
		state.showChangeLog = true;

	if (ImGui::MenuItem("Command-Line Help"))
		state.showCommandLineHelp = true;

	ImGui::MenuItem("Export Debugger Help...", nullptr, false, false);  // placeholder
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

void ATUIRenderMainMenu(ATSimulator &sim, SDL_Window *window, IDisplayBackend *backend, ATUIState &state) {
	// In fullscreen mode, hide the menu bar to match Windows Altirra behaviour.
	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (isFullscreen) {
		g_menuBarHeight = 0.0f;
		return;
	}

	if (!ImGui::BeginMainMenuBar())
		return;

	if (ImGui::BeginMenu("File")) { RenderFileMenu(sim, state, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("View")) { RenderViewMenu(sim, state, window, backend); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("System")) { RenderSystemMenu(sim, state); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Input")) { RenderInputMenu(sim, state); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Cheat")) { RenderCheatMenu(sim, state); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Debug")) { RenderDebugMenu(sim); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Record")) { RenderRecordMenu(sim, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Tools")) { RenderToolsMenu(sim, state, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Window")) { RenderWindowMenu(window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Help")) { RenderHelpMenu(state); ImGui::EndMenu(); }

	// Store the menu bar height so the display rect calculation can offset
	// the emulator screen below the menu bar.
	g_menuBarHeight = ImGui::GetWindowSize().y;

	ImGui::EndMainMenuBar();

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
			SDL_ShowSaveFileDialog(SAPSaveCallback, nullptr, window, kXEXFilters, 2, nullptr);
		}
		if (g_tapeNeedsSaveDialog) {
			g_tapeNeedsSaveDialog = false;
			static const SDL_DialogFileFilter kWAVFilters[] = {
				{ "WAV Audio", "wav" },
				{ "All Files", "*" },
			};
			SDL_ShowSaveFileDialog(TapeAnalysisSaveCallback, nullptr, window, kWAVFilters, 2, nullptr);
		}
	}
}
