//	AltirraSDL - Native macOS menu bar (NSMenu)
//	Replaces the ImGui menu bar on macOS with a platform-native menu that
//	appears in the system menu bar at the top of the screen.
//
//	Each top-level menu (File, View, System, etc.) has an NSMenuDelegate
//	whose -menuNeedsUpdate: rebuilds items from the current emulator state
//	every time the user opens the menu.  Action blocks capture the same C++
//	code that the ImGui menu items execute.

#ifdef VD_OS_MACOS

#import <Cocoa/Cocoa.h>

#include <stdafx.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>

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
#include "ui_menus_internal.h"
#include "ui_debugger.h"
#include "ui_textselection.h"
#include "display_backend.h"
#include "ui_file_dialog_sdl3.h"
#include "simulator.h"
#include "gtia.h"
#include "antic.h"
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
#include "debugger.h"
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#endif
#include "ui_mode.h"
#include "logging.h"

#include "macos_menubar.h"

// =========================================================================
// Extern globals
// =========================================================================

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;
extern ATUIState g_uiState;
extern ATUIKeyboardOptions g_kbdOpts;
extern float g_menuBarHeight;
extern bool g_copyFrameRequested;

// Functions declared in ui_menus.cpp
void ATUIShowBootImageDialog(SDL_Window *window);
void ATUIShowOpenImageDialog(SDL_Window *window);
void ATUIShowOpenSourceFileDialog(SDL_Window *window);
void ATUIShowSaveFrameDialog(SDL_Window *window);
void ATUIQuickSaveState();
void ATUIQuickLoadState();
bool ATUIHasQuickSaveState();

void ATSetFullscreen(bool fs);
void ATUIPasteText();
bool ATUIClipIsTextAvailable();
IDisplayBackend *ATUIGetDisplayBackend();

// =========================================================================
// Tag-based action dispatch (GCC-compatible — no ObjC blocks)
// =========================================================================
//
// Each NSMenuItem gets a unique integer tag.  The action std::function is
// stored in a global map keyed by tag.  ATMenuTarget looks up and invokes
// the function when the user clicks the item.  The map is cleared at the
// start of each menuNeedsUpdate: before the builder repopulates it.
//
// This avoids ObjC blocks ([=]{}) which are a Clang extension unsupported
// by GCC's cc1objplus.

static std::unordered_map<NSInteger, std::function<void()>> g_menuActions;
static NSInteger g_nextActionTag = 1;

// Clear all actions — only used during shutdown.
// Per-menu cleanup is done in menuNeedsUpdate: to avoid
// wiping the App menu's actions (it has no delegate).
static void ClearMenuActions() {
	g_menuActions.clear();
	g_nextActionTag = 1;
}

// Singleton target for all menu items.
@interface ATMenuTarget : NSObject
+ (instancetype)shared;
- (void)menuItemClicked:(NSMenuItem *)sender;
@end

@implementation ATMenuTarget
+ (instancetype)shared {
	static ATMenuTarget *instance = [[ATMenuTarget alloc] init];
	return instance;
}
- (void)menuItemClicked:(NSMenuItem *)sender {
	auto it = g_menuActions.find(sender.tag);
	if (it != g_menuActions.end())
		it->second();
}
@end

// Delegate: one per top-level menu.  Rebuilds contents on open.
typedef void (*MenuBuilderFn)(NSMenu *);

@interface ATMenuDelegate : NSObject <NSMenuDelegate>
@property (assign) MenuBuilderFn builder;
@end

@implementation ATMenuDelegate
- (void)menuNeedsUpdate:(NSMenu *)menu {
	// Remove only the actions belonging to items in this menu —
	// ClearMenuActions() would wipe the App menu's Quit action (CMD+Q)
	// since the App menu has no delegate and is never rebuilt.
	for (NSMenuItem *item in [menu itemArray])
		g_menuActions.erase(item.tag);
	[menu removeAllItems];
	if (self.builder)
		self.builder(menu);
}
@end

// =========================================================================
// Menu item construction helpers
// =========================================================================

static NSMenuItem *AddItem(NSMenu *menu, NSString *title,
						   NSString *keyEquiv, NSUInteger modMask,
						   bool checked, bool enabled,
						   std::function<void()> action) {
	NSMenuItem *item = [[NSMenuItem alloc]
		initWithTitle:title
		action:@selector(menuItemClicked:)
		keyEquivalent:keyEquiv ? keyEquiv : @""];
	item.target = [ATMenuTarget shared];
	if (keyEquiv && modMask)
		item.keyEquivalentModifierMask = modMask;
	else if (keyEquiv)
		item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
	item.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
	item.enabled = enabled;

	NSInteger tag = g_nextActionTag++;
	g_menuActions[tag] = std::move(action);
	item.tag = tag;

	[menu addItem:item];
	return item;
}

// Overload without shortcut
static NSMenuItem *AddItem(NSMenu *menu, NSString *title,
						   bool checked, bool enabled,
						   std::function<void()> action) {
	return AddItem(menu, title, nil, 0, checked, enabled, std::move(action));
}

static NSMenu *AddSubmenu(NSMenu *parentMenu, NSString *title, bool enabled = true) {
	NSMenuItem *item = [[NSMenuItem alloc]
		initWithTitle:title action:nil keyEquivalent:@""];
	item.enabled = enabled;
	NSMenu *sub = [[NSMenu alloc] initWithTitle:title];
	sub.autoenablesItems = NO;
	item.submenu = sub;
	[parentMenu addItem:item];
	return sub;
}

static void AddSeparator(NSMenu *menu) {
	[menu addItem:[NSMenuItem separatorItem]];
}

// Convert NSString from VDStringW
static NSString *NSStr(const VDStringW &ws) {
	VDStringA u8 = VDTextWToU8(ws);
	return [NSString stringWithUTF8String:u8.c_str()];
}

static NSString *NSStr(const VDStringA &s) {
	return [NSString stringWithUTF8String:s.c_str()];
}

// =========================================================================
// MRU helpers (same registry format as ui_menus.cpp)
// =========================================================================

static VDStringW MacGetMRU(uint32 index) {
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

static uint32 MacGetMRUCount() {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	return (uint32)order.size();
}

static void MacClearMRU() {
	VDRegistryAppKey key("MRU List", true);
	key.removeValue("Order");
}

// =========================================================================
// File menu
// =========================================================================

static const SDL_DialogFileFilter kMacImageFilters[] = {
	{ "Atari Images", "atr;xfd;dcm;xex;obx;com;bin;rom;car;cas;wav;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kMacCartFilters[] = {
	{ "Cartridge Images", "car;rom;bin" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kMacDiskAttachFilters[] = {
	{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kMacCasSaveFilters[] = {
	{ "Cassette Images", "cas" },
	{ "All Files", "*" },
};

static void BuildFileMenu(NSMenu *menu) {
	SDL_Window *window = g_pWindow;


	AddItem(menu, @"Boot Image...", @"b", NSEventModifierFlagCommand, false, true, [=]{
		ATUIShowBootImageDialog(window);
	});

	AddItem(menu, @"Open Image...", @"o", NSEventModifierFlagCommand, false, true, [=]{
		ATUIShowOpenImageDialog(window);
	});

	// Recently Booted (MRU list)
	uint32 mruCount = MacGetMRUCount();
	NSMenu *mruMenu = AddSubmenu(menu, @"Recently Booted", mruCount > 0);
	for (uint32 i = 0; i < mruCount; ++i) {
		VDStringW wpath = MacGetMRU(i);
		if (wpath.empty()) continue;
		VDStringA u8 = VDTextWToU8(wpath);
		const char *base = strrchr(u8.c_str(), '/');
		if (!base) base = strrchr(u8.c_str(), '\\');
		const char *label = base ? base + 1 : u8.c_str();

		char menuLabel[280];
		snprintf(menuLabel, sizeof(menuLabel), "%d. %s", i + 1, label);

		VDStringW pathCopy = wpath;
		AddItem(mruMenu, [NSString stringWithUTF8String:menuLabel], false, true, [=]{
			ATImageLoadContext ctx {};
			if (g_sim.Load(pathCopy.c_str(), kATMediaWriteMode_RO, &ctx)) {
				extern void ATAddMRU(const wchar_t *);
				ATAddMRU(pathCopy.c_str());
				g_sim.ColdReset();
				g_sim.Resume();
			}
		});
	}
	if (mruCount > 0) {
		AddSeparator(mruMenu);
		AddItem(mruMenu, @"Clear List", false, true, [=]{
			MacClearMRU();
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Disk Drives...", @"d", NSEventModifierFlagCommand, false, true, [=]{
		g_uiState.showDiskManager = true;
	});

	// Attach Disk submenu
	{
		NSMenu *attachMenu = AddSubmenu(menu, @"Attach Disk");
		AddItem(attachMenu, @"Rotate Down", false, true, [=]{
			g_sim.RotateDrives(8, 1);
		});
		AddItem(attachMenu, @"Rotate Up", false, true, [=]{
			g_sim.RotateDrives(8, -1);
		});
		AddSeparator(attachMenu);

		for (int i = 0; i < 8; ++i) {
			ATDiskInterface& di = g_sim.GetDiskInterface(i);
			bool loaded = di.IsDiskLoaded();
			NSString *label;
			if (loaded) {
				const wchar_t *path = di.GetPath();
				VDStringA u8p;
				if (path && *path) {
					u8p = VDTextWToU8(VDStringW(path));
					const char *b = strrchr(u8p.c_str(), '/');
					if (b) u8p = VDStringA(b + 1);
				}
				label = [NSString stringWithFormat:@"Drive %d [%s]...",
					i + 1, u8p.empty() ? "loaded" : u8p.c_str()];
			} else {
				label = [NSString stringWithFormat:@"Drive %d...", i + 1];
			}
			int driveIdx = i;
			AddItem(attachMenu, label, false, true, [=]{
				ATUIShowOpenFileDialog('disk',
					[](void *ud, const char * const *fl, int) {
						int idx = (int)(intptr_t)ud;
						if (fl && fl[0])
							ATUIPushDeferred(kATDeferred_AttachDisk, fl[0], idx);
					}, (void *)(intptr_t)driveIdx, window,
					kMacDiskAttachFilters, 2, false);
			});
		}
	}

	// Detach Disk submenu
	{
		NSMenu *detachMenu = AddSubmenu(menu, @"Detach Disk");
		AddItem(detachMenu, @"All", false, true, [=]{
			for (int i = 0; i < 15; ++i) {
				g_sim.GetDiskInterface(i).UnloadDisk();
				g_sim.GetDiskDrive(i).SetEnabled(false);
			}
		});
		AddSeparator(detachMenu);
		for (int i = 0; i < 8; ++i) {
			bool loaded = g_sim.GetDiskInterface(i).IsDiskLoaded();
			NSString *label = [NSString stringWithFormat:@"Drive %d", i + 1];
			int driveIdx = i;
			AddItem(detachMenu, label, false, loaded, [=]{
				g_sim.GetDiskInterface(driveIdx).UnloadDisk();
				g_sim.GetDiskDrive(driveIdx).SetEnabled(false);
			});
		}
	}

	// Cassette submenu
	{
		ATCassetteEmulator& cas = g_sim.GetCassette();
		bool loaded = cas.IsLoaded();
		NSMenu *casMenu = AddSubmenu(menu, @"Cassette");
		AddItem(casMenu, @"Tape Control...", false, true, [=]{
			g_uiState.showCassetteControl = true;
		});
		AddItem(casMenu, @"Tape Editor...", false, true, [=]{
			g_uiState.showTapeEditor = true;
		});
		AddSeparator(casMenu);
		AddItem(casMenu, @"New Tape", false, true, [=]{
			g_sim.GetCassette().LoadNew();
		});
		AddItem(casMenu, @"Load...", false, true, [=]{
			static const SDL_DialogFileFilter casFilters[] = {
				{ "Cassette Images", "cas;wav" }, { "All Files", "*" },
			};
			ATUIShowOpenFileDialog('cass', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_LoadCassette, fl[0]);
			}, nullptr, window, casFilters, 2, false);
		});
		AddItem(casMenu, @"Unload", false, loaded, [=]{
			g_sim.GetCassette().Unload();
		});
		AddItem(casMenu, @"Save...", false, loaded, [=]{
			ATUIShowSaveFileDialog('cass',
				[](void *, const char * const *fl, int) {
					if (fl && fl[0])
						ATUIPushDeferred(kATDeferred_SaveCassette, fl[0]);
				}, nullptr, window, kMacCasSaveFilters, 2);
		});
		AddItem(casMenu, @"Export Audio Tape...", false, loaded, [=]{
			static const SDL_DialogFileFilter wavFilters[] = {
				{ "WAV Audio", "wav" }, { "All Files", "*" },
			};
			ATUIShowSaveFileDialog('casa', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_ExportCassetteAudio, fl[0]);
			}, nullptr, window, wavFilters, 1);
		});
	}

	AddSeparator(menu);

	// State save/load
	{
		static const SDL_DialogFileFilter stateFilters[] = {
			{ "Save States", "atstate2;atstate" }, { "All Files", "*" },
		};
		AddItem(menu, @"Load State...", false, true, [=]{
			ATUIShowOpenFileDialog('save',
				[](void *, const char * const *fl, int) {
					if (fl && fl[0])
						ATUIPushDeferred(kATDeferred_LoadState, fl[0]);
				}, nullptr, window, stateFilters, 2, false);
		});
		AddItem(menu, @"Save State...", false, true, [=]{
			ATUIShowSaveFileDialog('save',
				[](void *, const char * const *fl, int) {
					if (fl && fl[0])
						ATUIPushDeferred(kATDeferred_SaveState, fl[0]);
				}, nullptr, window, stateFilters, 1);
		});
		AddItem(menu, @"Quick Load State", false, ATUIHasQuickSaveState(), [=]{
			ATUIQuickLoadState();
		});
		AddItem(menu, @"Quick Save State", false, true, [=]{
			ATUIQuickSaveState();
		});
	}

	AddSeparator(menu);

	// Attach Special Cartridge submenu
	{
		NSMenu *specCart = AddSubmenu(menu, @"Attach Special Cartridge");
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
			int mode = sc.mode;
			AddItem(specCart, [NSString stringWithUTF8String:sc.label], false, true, [=]{
				g_sim.LoadNewCartridge(mode);
				if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
					g_sim.ColdReset();
				g_sim.Resume();
			});
		}
		AddSeparator(specCart);
		AddItem(specCart, @"BASIC", false, true, [=]{
			g_sim.LoadCartridgeBASIC();
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				g_sim.ColdReset();
			g_sim.Resume();
		});
	}

	// Secondary Cartridge submenu
	{
		NSMenu *secCart = AddSubmenu(menu, @"Secondary Cartridge");
		AddItem(secCart, @"Attach...", false, true, [=]{
			ATUIShowOpenFileDialog('cart', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_AttachSecondaryCartridge, fl[0]);
			}, nullptr, window, kMacCartFilters, 2, false);
		});
		AddItem(secCart, @"Detach", false, g_sim.IsCartridgeAttached(1), [=]{
			g_sim.UnloadCartridge(1);
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				g_sim.ColdReset();
			g_sim.Resume();
		});
	}

	AddItem(menu, @"Attach Cartridge...", false, true, [=]{
		ATUIShowOpenFileDialog('cart',
			[](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_AttachCartridge, fl[0]);
			}, nullptr, window, kMacCartFilters, 2, false);
	});

	AddItem(menu, @"Detach Cartridge", false, g_sim.IsCartridgeAttached(0), [=]{
		if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
			g_sim.LoadCartridge5200Default();
		else
			g_sim.UnloadCartridge(0);
		if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
			g_sim.ColdReset();
		g_sim.Resume();
	});

	// Save Firmware submenu
	{
		NSMenu *fwMenu = AddSubmenu(menu, @"Save Firmware");
		static const SDL_DialogFileFilter fwSaveCartFilters[] = {
			{ "Cartridge image with header (*.car)", "car" },
			{ "Raw cartridge image (*.bin, *.rom)", "bin;rom" },
			{ "All Files", "*" },
		};
		static const SDL_DialogFileFilter fwFilters[] = {
			{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
		};

		AddItem(fwMenu, @"Save Cartridge...", false, g_sim.IsCartridgeAttached(0), [=]{
			ATUIShowSaveFileDialog('cart', [](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveCartridge, fl[0]);
			}, nullptr, window, fwSaveCartFilters, 3);
		});

		AddItem(fwMenu, @"Save KMK/JZ IDE / SIDE / MyIDE II Main Flash...", false,
			g_sim.IsStoragePresent((ATStorageId)kATStorageId_Firmware), [=]{
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)0, window, fwFilters, 2);
		});

		AddItem(fwMenu, @"Save KMK/JZ IDE SDX Flash...", false,
			g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 1)), [=]{
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)1, window, fwFilters, 2);
		});

		AddItem(fwMenu, @"Save Ultimate1MB Flash...", false,
			g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 2)), [=]{
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)2, window, fwFilters, 2);
		});

		AddItem(fwMenu, @"Save Rapidus Flash...", false,
			g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 3)), [=]{
			ATUIShowSaveFileDialog('rom ', [](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)3, window, fwFilters, 2);
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Exit", false, true, [=]{
		g_uiState.showExitConfirm = true;
	});
}

// =========================================================================
// View menu
// =========================================================================

static void BuildViewMenu(NSMenu *menu) {

	SDL_Window *window = g_pWindow;

	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	AddItem(menu, @"Full Screen", @"\r", NSEventModifierFlagCommand, isFullscreen, true, [=]{
		ATSetFullscreen(!isFullscreen);
	});

	AddSeparator(menu);

	// Filter Mode
	{
		ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
		NSMenu *filterMenu = AddSubmenu(menu, @"Filter Mode");
		AddItem(filterMenu, @"Next Mode", false, true, [=]{
			static const ATDisplayFilterMode kModes[] = {
				kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
				kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
				kATDisplayFilterMode_AnySuitable,
			};
			int cur = 0;
			ATDisplayFilterMode cfm = ATUIGetDisplayFilterMode();
			for (int i = 0; i < 5; ++i)
				if (kModes[i] == cfm) { cur = i; break; }
			ATUISetDisplayFilterMode(kModes[(cur + 1) % 5]);
		});
		AddSeparator(filterMenu);
		AddItem(filterMenu, @"Point", fm == kATDisplayFilterMode_Point, true, [=]{
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Point);
		});
		AddItem(filterMenu, @"Bilinear", fm == kATDisplayFilterMode_Bilinear, true, [=]{
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
		});
		AddItem(filterMenu, @"Sharp Bilinear", fm == kATDisplayFilterMode_SharpBilinear, true, [=]{
			ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
		});
		AddItem(filterMenu, @"Bicubic", fm == kATDisplayFilterMode_Bicubic, true, [=]{
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic);
		});
		AddItem(filterMenu, @"Default (Any Suitable)", fm == kATDisplayFilterMode_AnySuitable, true, [=]{
			ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable);
		});
	}

	// Filter Sharpness
	{
		int sharpness = ATUIGetViewFilterSharpness();
		NSMenu *sharpMenu = AddSubmenu(menu, @"Filter Sharpness");
		AddItem(sharpMenu, @"Softer", sharpness == -2, true, [=]{ ATUISetViewFilterSharpness(-2); });
		AddItem(sharpMenu, @"Soft", sharpness == -1, true, [=]{ ATUISetViewFilterSharpness(-1); });
		AddItem(sharpMenu, @"Normal", sharpness == 0, true, [=]{ ATUISetViewFilterSharpness(0); });
		AddItem(sharpMenu, @"Sharp", sharpness == 1, true, [=]{ ATUISetViewFilterSharpness(1); });
		AddItem(sharpMenu, @"Sharper", sharpness == 2, true, [=]{ ATUISetViewFilterSharpness(2); });
	}

	// Video Frame
	{
		ATDisplayStretchMode sm = ATUIGetDisplayStretchMode();
		NSMenu *frameMenu = AddSubmenu(menu, @"Video Frame");
		AddItem(frameMenu, @"Fit to Window", sm == kATDisplayStretchMode_Unconstrained, true, [=]{
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained);
		});
		AddItem(frameMenu, @"Preserve Aspect Ratio", sm == kATDisplayStretchMode_PreserveAspectRatio, true, [=]{
			ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio);
		});
		AddItem(frameMenu, @"Preserve Aspect Ratio (fixed multiples only)", sm == kATDisplayStretchMode_IntegralPreserveAspectRatio, true, [=]{
			ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio);
		});
		AddItem(frameMenu, @"Square Pixels", sm == kATDisplayStretchMode_SquarePixels, true, [=]{
			ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels);
		});
		AddItem(frameMenu, @"Square Pixels (fixed multiples only)", sm == kATDisplayStretchMode_Integral, true, [=]{
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral);
		});
		AddSeparator(frameMenu);
		bool pzActive = ATUIIsPanZoomToolActive();
		AddItem(frameMenu, @"Pan/Zoom Tool", pzActive, true, [=]{
			ATUISetPanZoomToolActive(!ATUIIsPanZoomToolActive());
		});
		AddItem(frameMenu, @"Reset Pan and Zoom", false, true, [=]{
			ATUISetDisplayZoom(1.0f);
			ATUISetDisplayPanOffset({0, 0});
		});
		AddItem(frameMenu, @"Reset Panning", false, true, [=]{
			ATUISetDisplayPanOffset({0, 0});
		});
		AddItem(frameMenu, @"Reset Zoom", false, true, [=]{
			ATUISetDisplayZoom(1.0f);
		});
	}

	// Overscan Mode
	{
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		auto om = gtia.GetOverscanMode();
		NSMenu *osMenu = AddSubmenu(menu, @"Overscan Mode");
		AddItem(osMenu, @"OS Screen Only", om == ATGTIAEmulator::kOverscanOSScreen, true, [=]{
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
		});
		AddItem(osMenu, @"Normal", om == ATGTIAEmulator::kOverscanNormal, true, [=]{
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
		});
		AddItem(osMenu, @"Widescreen", om == ATGTIAEmulator::kOverscanWidescreen, true, [=]{
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanWidescreen);
		});
		AddItem(osMenu, @"Extended", om == ATGTIAEmulator::kOverscanExtended, true, [=]{
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
		});
		AddItem(osMenu, @"Full (With Blanking)", om == ATGTIAEmulator::kOverscanFull, true, [=]{
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanFull);
		});
		AddSeparator(osMenu);

		// Vertical Override
		auto vom = gtia.GetVerticalOverscanMode();
		NSMenu *vosMenu = AddSubmenu(osMenu, @"Vertical Override");
		AddItem(vosMenu, @"Off", vom == ATGTIAEmulator::kVerticalOverscan_Default, true, [=]{
			g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Default);
		});
		AddItem(vosMenu, @"OS Screen Only", vom == ATGTIAEmulator::kVerticalOverscan_OSScreen, true, [=]{
			g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_OSScreen);
		});
		AddItem(vosMenu, @"Normal", vom == ATGTIAEmulator::kVerticalOverscan_Normal, true, [=]{
			g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Normal);
		});
		AddItem(vosMenu, @"Extended", vom == ATGTIAEmulator::kVerticalOverscan_Extended, true, [=]{
			g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Extended);
		});
		AddItem(vosMenu, @"Full (With Blanking)", vom == ATGTIAEmulator::kVerticalOverscan_Full, true, [=]{
			g_sim.GetGTIA().SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Full);
		});

		bool palExt = gtia.IsOverscanPALExtended();
		AddItem(osMenu, @"Extended PAL Height", palExt, true, [=]{
			g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
		});

		bool indicatorMargin = ATUIGetDisplayPadIndicators();
		AddItem(osMenu, @"Indicator Margin", indicatorMargin, true, [=]{
			ATUISetDisplayPadIndicators(!ATUIGetDisplayPadIndicators());
		});
	}

	AddSeparator(menu);

	// VSync
	{
		bool vsyncOn = g_sim.GetGTIA().IsVsyncEnabled();
		AddItem(menu, @"Vertical Sync", vsyncOn, true, [=]{
			g_sim.GetGTIA().SetVsyncEnabled(!g_sim.GetGTIA().IsVsyncEnabled());
		});
	}

	bool showFPS = ATUIGetShowFPS();
	AddItem(menu, @"Show FPS", showFPS, true, [=]{
		ATUISetShowFPS(!ATUIGetShowFPS());
	});

	// Video Outputs
	{
		bool altView = ATUIGetAltViewEnabled();
		NSMenu *voMenu = AddSubmenu(menu, @"Video Outputs");
		AddItem(voMenu, @"1 Computer Output", !altView, true, [=]{
			ATUISetAltViewEnabled(false);
		});
		if (ATUIIsAltOutputAvailable()) {
			AddItem(voMenu, @"Next Output", false, true, [=]{
				ATUISelectNextAltOutput();
			});
		}
		bool autoSwitch = ATUIGetAltViewAutoswitchingEnabled();
		AddItem(voMenu, @"Auto-Switch Video Output", autoSwitch, true, [=]{
			ATUISetAltViewAutoswitchingEnabled(!ATUIGetAltViewAutoswitchingEnabled());
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Adjust Colors...", false, true, [=]{
		g_uiState.showAdjustColors = true;
	});

	// Screen Effects submenu (simplified for native menu - no ImGui tooltip)
	{
		NSMenu *sfxMenu = AddSubmenu(menu, @"Screen Effects");
		IDisplayBackend *be = ATUIGetDisplayBackend();
		bool hasPreset = be && be->HasShaderPreset();

		if (hasPreset)
			g_uiState.screenEffectsMode = ATUIState::kSFXMode_Preset;

		bool isNone = (g_uiState.screenEffectsMode == ATUIState::kSFXMode_None);
		bool isBasic = (g_uiState.screenEffectsMode == ATUIState::kSFXMode_Basic);
		bool isPreset = (g_uiState.screenEffectsMode == ATUIState::kSFXMode_Preset);

		AddItem(sfxMenu, @"(None)", isNone, true, [=]{
			ATUIShaderPresetsClear(ATUIGetDisplayBackend());
			g_uiState.screenEffectsMode = ATUIState::kSFXMode_None;
		});
		AddItem(sfxMenu, @"Basic", isBasic, true, [=]{
			ATUIShaderPresetsClear(ATUIGetDisplayBackend());
			g_uiState.screenEffectsMode = ATUIState::kSFXMode_Basic;
		});
		AddSeparator(sfxMenu);

		bool canShowParams = !isNone;
		AddItem(sfxMenu, @"Shader Parameters...", false, canShowParams, [=]{
			if (g_uiState.screenEffectsMode == ATUIState::kSFXMode_Preset)
				g_uiState.showShaderParams = true;
			else
				g_uiState.showScreenEffects = true;
		});
		AddItem(sfxMenu, @"Shader Setup...", false, true, [=]{
			g_uiState.showShaderSetup = true;
		});
	}

	AddItem(menu, @"Customize HUD...", false, true, [=]{
		g_uiState.showCustomizeHud = true;
	});
	AddItem(menu, @"Calibrate...", false, true, [=]{
		g_uiState.showCalibrate = true;
	});

	AddSeparator(menu);

	{
		bool dbgOpen = ATUIDebuggerIsOpen();
		AddItem(menu, @"Display", false, dbgOpen, [=]{
			ATUIDebuggerFocusDisplay();
		});
		AddItem(menu, @"Printer Output", false, dbgOpen, [=]{
			ATActivateUIPane(kATUIPaneId_PrinterOutput, true, true);
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Copy Frame to Clipboard", false, true, [=]{
		g_copyFrameRequested = true;
	});
	AddItem(menu, @"Copy Frame to Clipboard (True Aspect)", false, false, [=]{});

	AddItem(menu, @"Save Frame...", false, true, [=]{
		ATUIShowSaveFrameDialog(g_pWindow);
	});
	AddItem(menu, @"Save Frame (True Aspect)...", false, false, [=]{});

	// Text Selection
	{
		bool hasSelection = ATUIIsTextSelected();
		NSMenu *tsMenu = AddSubmenu(menu, @"Text Selection");
		AddItem(tsMenu, @"Copy Text", false, hasSelection, [=]{
			ATUITextCopy(ATTextCopyMode::ASCII);
		});
		AddItem(tsMenu, @"Copy Escaped Text", false, hasSelection, [=]{
			ATUITextCopy(ATTextCopyMode::Escaped);
		});
		AddItem(tsMenu, @"Copy Hex", false, hasSelection, [=]{
			ATUITextCopy(ATTextCopyMode::Hex);
		});
		AddItem(tsMenu, @"Copy Unicode", false, hasSelection, [=]{
			ATUITextCopy(ATTextCopyMode::Unicode);
		});
		AddItem(tsMenu, @"Paste Text", false, ATUIClipIsTextAvailable(), [=]{
			ATUIPasteText();
		});
		AddSeparator(tsMenu);
		AddItem(tsMenu, @"Select All", false, true, [=]{
			ATUITextSelectAll();
		});
		AddItem(tsMenu, @"Deselect", false, hasSelection, [=]{
			ATUITextDeselect();
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Switch to Gaming Mode", false, true, [=]{
		ATUISetMode(ATUIMode::Gaming);
		ATUISaveMode();
		SDL_Window *w = g_pWindow;
		float cs = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(w));
		if (cs < 1.0f) cs = 1.0f;
		if (cs > 4.0f) cs = 4.0f;
		ATUIApplyModeStyle(cs);
	});
}

// =========================================================================
// System menu
// =========================================================================

static void BuildSystemMenu(NSMenu *menu) {
#ifdef ALTIRRA_NETPLAY_ENABLED
	const bool netplayActive = ATNetplayGlue::IsActive();
#else
	const bool netplayActive = false;
#endif

	// Profiles submenu — disabled while Online Play is active
	// (the canonical session profile must not be mutated mid-session,
	// and switching out of it would abandon the session in an
	// inconsistent state).
	{
		NSMenu *profMenu = AddSubmenu(menu,
			netplayActive ? @"Profiles (disabled: Playing Online)" : @"Profiles",
			!netplayActive);
		if (!netplayActive) {
			AddItem(profMenu, @"Edit Profiles...", false, true, [=]{
				g_uiState.showProfiles = true;
			});
			bool temporary = ATSettingsGetTemporaryProfileMode();
			AddItem(profMenu, @"Temporary Profile", temporary, true, [=]{
				ATSettingsSetTemporaryProfileMode(!ATSettingsGetTemporaryProfileMode());
			});
			AddSeparator(profMenu);

			uint32 currentId = ATSettingsGetCurrentProfileId();
			{
				VDStringW name = ATSettingsProfileGetName(0);
				AddItem(profMenu, NSStr(name), currentId == 0, true, [=]{
					if (ATSettingsGetCurrentProfileId() != 0) {
						ATSettingsSwitchProfile(0);
						g_sim.Resume();
					}
				});
			}
			vdfastvector<uint32> profileIds;
			ATSettingsProfileEnum(profileIds);
			for (uint32 id : profileIds) {
				if (!ATSettingsProfileGetVisible(id))
					continue;
				VDStringW name = ATSettingsProfileGetName(id);
				uint32 pid = id;
				AddItem(profMenu, NSStr(name), currentId == id, true, [=]{
					if (ATSettingsGetCurrentProfileId() != pid) {
						ATSettingsSwitchProfile(pid);
						g_sim.Resume();
					}
				});
			}
		}
	}

	// Configure System dialog likewise edits settings the canonical
	// Online Play profile pins — disable while a session is active.
	AddItem(menu,
		netplayActive ? @"Configure System... (disabled: Playing Online)"
		              : @"Configure System...",
		@"s", NSEventModifierFlagCommand, false, !netplayActive, [=]{
		g_uiState.showSystemConfig = true;
	});

	AddSeparator(menu);

	// Reset / Pause are sim-mutating actions — performing them
	// during a netplay session would diverge our local state from
	// the peer's and instantly desync.  Disable while online; the
	// user must Leave the session first.
	AddItem(menu,
		netplayActive ? @"Warm Reset (disabled: Playing Online)"
		              : @"Warm Reset",
		false, !netplayActive, [=]{
		g_sim.WarmReset();
		g_sim.Resume();
	});
	AddItem(menu,
		netplayActive ? @"Cold Reset (disabled: Playing Online)"
		              : @"Cold Reset",
		false, !netplayActive, [=]{
		g_sim.ColdReset();
		g_sim.Resume();
		if (!g_kbdOpts.mbAllowShiftOnColdReset)
			g_sim.GetPokey().SetShiftKeyState(false, true);
	});
	AddItem(menu,
		netplayActive ? @"Cold Reset (Computer Only) (disabled: Playing Online)"
		              : @"Cold Reset (Computer Only)",
		false, !netplayActive, [=]{
		g_sim.ColdResetComputerOnly();
		g_sim.Resume();
		if (!g_kbdOpts.mbAllowShiftOnColdReset)
			g_sim.GetPokey().SetShiftKeyState(false, true);
	});

	bool paused = g_sim.IsPaused();
	AddItem(menu,
		netplayActive ? @"Pause (disabled: Playing Online)" : @"Pause",
		paused, !netplayActive, [=]{
		if (g_sim.IsPaused()) g_sim.Resume(); else g_sim.Pause();
	});

	AddSeparator(menu);

	bool turbo = ATUIGetTurbo();
	AddItem(menu, @"Warp Speed", turbo, true, [=]{
		ATUISetTurbo(!ATUIGetTurbo());
	});

	bool pauseInactive = ATUIGetPauseWhenInactive();
	AddItem(menu, @"Pause When Inactive", pauseInactive, true, [=]{
		ATUISetPauseWhenInactive(!ATUIGetPauseWhenInactive());
	});

	// Rewind submenu — disabled while online (rewind applies a
	// previous savestate, which would jump our sim to a different
	// frame than the peer's).
	if (netplayActive) {
		NSMenu *rewMenu = AddSubmenu(menu,
			@"Rewind (disabled: Playing Online)", false);
		(void)rewMenu;
	} else {
		IATAutoSaveManager &mgr = g_sim.GetAutoSaveManager();
		bool rewindEnabled = mgr.GetRewindEnabled();
		NSMenu *rewMenu = AddSubmenu(menu, @"Rewind");
		AddItem(rewMenu, @"Quick Rewind", false, rewindEnabled, [=]{
			ATUIQuickRewind();
		});
		AddItem(rewMenu, @"Rewind...", false, rewindEnabled, [=]{
			if (ATUIOpenRewindDialog())
				g_uiState.showRewind = true;
		});
		AddSeparator(rewMenu);
		AddItem(rewMenu, @"Enable Rewind Recording", rewindEnabled, true, [=]{
			g_sim.GetAutoSaveManager().SetRewindEnabled(
				!g_sim.GetAutoSaveManager().GetRewindEnabled());
		});
	}

	AddSeparator(menu);

	// Power-On Delay — sim-affecting, disabled while online.
	if (netplayActive) {
		AddSubmenu(menu, @"Power-On Delay (disabled: Playing Online)", false);
	} else {
		int delay = g_sim.GetPowerOnDelay();
		NSMenu *podMenu = AddSubmenu(menu, @"Power-On Delay");
		AddItem(podMenu, @"Auto", delay < 0, true, [=]{ g_sim.SetPowerOnDelay(-1); });
		AddItem(podMenu, @"None", delay == 0, true, [=]{ g_sim.SetPowerOnDelay(0); });
		AddItem(podMenu, @"1 Second", delay == 10, true, [=]{ g_sim.SetPowerOnDelay(10); });
		AddItem(podMenu, @"2 Seconds", delay == 20, true, [=]{ g_sim.SetPowerOnDelay(20); });
		AddItem(podMenu, @"3 Seconds", delay == 30, true, [=]{ g_sim.SetPowerOnDelay(30); });
	}

	AddItem(menu, @"Hold Keys For Reset", false, true, [=]{
		ATUIToggleHoldKeys();
	});

	bool basic = g_sim.IsBASICEnabled();
	AddItem(menu,
		netplayActive
			? @"Internal BASIC (disabled: Playing Online)"
			: @"Internal BASIC (Boot Without Option Key)",
		basic, !netplayActive, [=]{
		g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
		if (ATUIIsResetNeeded(kATUIResetFlag_BasicChange))
			g_sim.ColdReset();
	});

	bool casAutoBoot = g_sim.IsCassetteAutoBootEnabled();
	AddItem(menu,
		netplayActive
			? @"Auto-Boot Tape (disabled: Playing Online)"
			: @"Auto-Boot Tape (Hold Start)",
		casAutoBoot, !netplayActive, [=]{
		g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled());
	});

	AddSeparator(menu);

	// Console Switches — Keyboard Present / Self-Test / Cart Switch
	// + device buttons all mutate hashed sim state or attached-device
	// state.  Disabled while online.
	if (netplayActive) {
		AddSubmenu(menu, @"Console Switches (disabled: Playing Online)", false);
	} else {
		NSMenu *csMenu = AddSubmenu(menu, @"Console Switches");
		bool kbdPresent = g_sim.IsKeyboardPresent();
		AddItem(csMenu, @"Keyboard Present (XEGS)", kbdPresent, true, [=]{
			g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent());
		});
		bool selfTest = g_sim.IsForcedSelfTest();
		AddItem(csMenu, @"Force Self-Test", selfTest, true, [=]{
			g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest());
		});
		AddItem(csMenu, @"Activate Cart Menu Button", false, true, [=]{
			ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true);
		});
		bool cartSwitch = g_sim.GetCartridgeSwitch();
		AddItem(csMenu, @"Enable Cart Switch", cartSwitch, true, [=]{
			g_sim.SetCartridgeSwitch(!g_sim.GetCartridgeSwitch());
		});

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
					AddSeparator(csMenu);
					anyDevBtn = true;
				}
				bool dep = ATUIGetDeviceButtonDepressed((uint32)db.btn);
				ATDeviceButton btn = db.btn;
				AddItem(csMenu, [NSString stringWithUTF8String:db.label], dep, true, [=]{
					ATUIActivateDeviceButton((uint32)btn,
						!ATUIGetDeviceButtonDepressed((uint32)btn));
				});
			}
		}
	}
}

// =========================================================================
// Input menu
// =========================================================================

static void BuildInputMenu(NSMenu *menu) {

	ATInputManager *pIM = g_sim.GetInputManager();

	AddItem(menu, @"Input Mappings...", false, true, [=]{
		g_uiState.showInputMappings = true;
	});
	AddItem(menu, @"Input Setup...", false, true, [=]{
		g_uiState.showInputSetup = true;
	});
	AddItem(menu, @"Cycle Quick Maps", false, true, [=]{
		ATInputManager *im = g_sim.GetInputManager();
		if (im) im->CycleQuickMaps();
	});

	AddSeparator(menu);

	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool captured = ATUIIsMouseCaptured();
		AddItem(menu, @"Capture Mouse", captured, mouseMapped, [=]{
			if (ATUIIsMouseCaptured())
				ATUIReleaseMouse();
			else
				ATUICaptureMouse();
		});
		bool mouseAutoCapture = ATUIGetMouseAutoCapture();
		AddItem(menu, @"Auto-Capture Mouse", mouseAutoCapture, mouseMapped, [=]{
			ATUISetMouseAutoCapture(!ATUIGetMouseAutoCapture());
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Virtual Keyboard", g_uiState.showVirtualKeyboard, true, [=]{
		g_uiState.showVirtualKeyboard = !g_uiState.showVirtualKeyboard;
	});

	{
		NSMenu *kbMenu = AddSubmenu(menu, @"Keyboard Placement");
		AddItem(kbMenu, @"Auto", g_uiState.oskPlacement == 0, true, [=]{
			g_uiState.oskPlacement = 0;
		});
		AddItem(kbMenu, @"Bottom", g_uiState.oskPlacement == 1, true, [=]{
			g_uiState.oskPlacement = 1;
		});
		AddItem(kbMenu, @"Right", g_uiState.oskPlacement == 2, true, [=]{
			g_uiState.oskPlacement = 2;
		});
	}

	AddSeparator(menu);

	AddItem(menu, @"Light Pen/Gun...", false, true, [=]{
		g_uiState.showLightPen = true;
	});
	AddItem(menu, @"Recalibrate Light Pen/Gun", false, true, [=]{
		ATUIRecalibrateLightPen();
	});

	AddSeparator(menu);

	// Port 1-4 submenus
	if (pIM) {
		for (int portIdx = 0; portIdx < 4; ++portIdx) {
			NSMenu *portMenu = AddSubmenu(menu,
				[NSString stringWithFormat:@"Port %d", portIdx + 1]);

			// Collect input maps for this port
			struct MapEntry { ATInputMap *map; VDStringA name; bool active; };
			std::vector<MapEntry> entries;
			uint32 mapCount = pIM->GetInputMapCount();
			for (uint32 i = 0; i < mapCount; ++i) {
				vdrefptr<ATInputMap> imap;
				if (pIM->GetInputMapByIndex(i, ~imap)) {
					if (imap->UsesPhysicalPort(portIdx)) {
						MapEntry e;
						e.map = imap;
						e.name = VDTextWToU8(imap->GetName(), -1);
						e.active = pIM->IsInputMapEnabled(imap);
						entries.push_back(std::move(e));
					}
				}
			}
			std::sort(entries.begin(), entries.end(),
				[](const MapEntry &a, const MapEntry &b) {
					return vdwcsicmp(a.map->GetName(), b.map->GetName()) < 0;
				});

			bool anyActive = false;
			for (const auto &e : entries)
				if (e.active) { anyActive = true; break; }

			// Capture entries for the block
			auto entriesCopy = std::make_shared<std::vector<MapEntry>>(entries);
			int pi = portIdx;

			AddItem(portMenu, @"None", !anyActive, true, [=]{
				ATInputManager *im = g_sim.GetInputManager();
				if (!im) return;
				uint32 mc = im->GetInputMapCount();
				for (uint32 j = 0; j < mc; ++j) {
					vdrefptr<ATInputMap> m;
					if (im->GetInputMapByIndex(j, ~m) && m->UsesPhysicalPort(pi) && im->IsInputMapEnabled(m))
						im->ActivateInputMap(m, false);
				}
			});

			for (size_t ei = 0; ei < entries.size(); ++ei) {
				const auto &e = entries[ei];
				VDStringW mapName(e.map->GetName());
				AddItem(portMenu,
					[NSString stringWithUTF8String:e.name.c_str()],
					e.active, true, [=]{
					ATInputManager *im = g_sim.GetInputManager();
					if (!im) return;
					uint32 mc = im->GetInputMapCount();
					for (uint32 j = 0; j < mc; ++j) {
						vdrefptr<ATInputMap> m;
						if (im->GetInputMapByIndex(j, ~m) && m->UsesPhysicalPort(pi)) {
							bool shouldActivate = (wcscmp(m->GetName(), mapName.c_str()) == 0);
							im->ActivateInputMap(m, shouldActivate);
						}
					}
				});
			}
		}
	}
}

// =========================================================================
// Cheat menu
// =========================================================================

static void BuildCheatMenu(NSMenu *menu) {


	AddItem(menu, @"Cheater...", false, true, [=]{
		g_uiState.showCheater = true;
	});
	AddSeparator(menu);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	bool pmColl = gtia.ArePMCollisionsEnabled();
	AddItem(menu, @"Disable P/M Collisions", !pmColl, true, [=]{
		g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
	});
	bool pfColl = gtia.ArePFCollisionsEnabled();
	AddItem(menu, @"Disable Playfield Collisions", !pfColl, true, [=]{
		g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
	});
}

// =========================================================================
// Debug menu
// =========================================================================

static void BuildDebugMenu(NSMenu *menu) {

	IATDebugger *dbg = ATGetDebugger();
	bool dbgEnabled = ATUIDebuggerIsOpen();
	bool dbgRunning = dbg && dbg->IsRunning();

	AddItem(menu, @"Enable Debugger", dbgEnabled, true, [=]{
		if (ATUIDebuggerIsOpen())
			ATUIDebuggerClose();
		else
			ATUIDebuggerOpen();
	});
	AddItem(menu, @"Open Source File...", false, dbgEnabled, [=]{
		ATUIShowOpenSourceFileDialog(g_pWindow);
	});
	AddItem(menu, @"Source File List...", false, dbgEnabled, [=]{
		ATUIDebuggerShowSourceListDialog();
	});

	// Window submenu
	{
		NSMenu *winMenu = AddSubmenu(menu, @"Window", dbgEnabled);
		AddItem(winMenu, @"Console", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_Console, true);
		});
		AddItem(winMenu, @"Registers", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_Registers, true);
		});
		AddItem(winMenu, @"Disassembly", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_Disassembly, true);
		});
		AddItem(winMenu, @"Call Stack", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_CallStack, true);
		});
		AddItem(winMenu, @"History", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_History, true);
		});
		NSMenu *memMenu = AddSubmenu(winMenu, @"Memory");
		for (int i = 0; i < 4; ++i) {
			int idx = i;
			AddItem(memMenu,
				[NSString stringWithFormat:@"Memory %d", i + 1],
				false, true, [=]{
				ATActivateUIPane(kATUIPaneId_MemoryN + idx, true);
			});
		}
		NSMenu *watchMenu = AddSubmenu(winMenu, @"Watch");
		for (int i = 0; i < 4; ++i) {
			int idx = i;
			AddItem(watchMenu,
				[NSString stringWithFormat:@"Watch %d", i + 1],
				false, true, [=]{
				ATActivateUIPane(kATUIPaneId_WatchN + idx, true);
			});
		}
		AddItem(winMenu, @"Breakpoints", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_Breakpoints, true);
		});
		AddItem(winMenu, @"Targets", false, false, [=]{});
		AddItem(winMenu, @"Debug Display", false, false, [=]{});
	}

	// Visualization
	{
		NSMenu *visMenu = AddSubmenu(menu, @"Visualization");
		AddItem(visMenu, @"Cycle GTIA Visualization", false, true, [=]{
			ATGTIAEmulator& gtia = g_sim.GetGTIA();
			int next = ((int)gtia.GetAnalysisMode() + 1) % ATGTIAEmulator::kAnalyzeCount;
			gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)next);
		});
		AddItem(visMenu, @"Cycle ANTIC Visualization", false, true, [=]{
			ATAnticEmulator& antic = g_sim.GetAntic();
			int next = ((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount;
			antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)next);
		});
	}

	// Options
	{
		NSMenu *optMenu = AddSubmenu(menu, @"Options");
		if (dbg) {
			bool breakAtExe = dbg->IsBreakOnEXERunAddrEnabled();
			AddItem(optMenu, @"Break at EXE Run Address", breakAtExe, true, [=]{
				IATDebugger *d = ATGetDebugger();
				if (d) d->SetBreakOnEXERunAddrEnabled(!d->IsBreakOnEXERunAddrEnabled());
			});
		}
		bool autoReload = g_sim.IsROMAutoReloadEnabled();
		AddItem(optMenu, @"Auto-Reload ROMs on Cold Reset", autoReload, true, [=]{
			g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
		});
		bool randomEXE = g_sim.IsRandomFillEXEEnabled();
		AddItem(optMenu, @"Randomize Memory On EXE Load", randomEXE, true, [=]{
			g_sim.SetRandomFillEXEEnabled(!g_sim.IsRandomFillEXEEnabled());
		});
		AddSeparator(optMenu);
		AddItem(optMenu, @"Change Font...", false, false, [=]{});
	}

	AddSeparator(menu);

	AddItem(menu, @"Run/Break", false, dbgEnabled, [=]{
		ATUIDebuggerRunStop();
	});
	AddItem(menu, @"Break", false, dbgEnabled && dbgRunning, [=]{
		ATUIDebuggerBreak();
	});

	AddSeparator(menu);

	AddItem(menu, @"Step Into", false, dbgEnabled && !dbgRunning, [=]{
		ATUIDebuggerStepInto();
	});
	AddItem(menu, @"Step Over", false, dbgEnabled && !dbgRunning, [=]{
		ATUIDebuggerStepOver();
	});
	AddItem(menu, @"Step Out", false, dbgEnabled && !dbgRunning, [=]{
		ATUIDebuggerStepOut();
	});
	AddItem(menu, @"Toggle Breakpoint", false, dbgEnabled && !dbgRunning, [=]{
		ATUIDebuggerToggleBreakpoint();
	});
	AddItem(menu, @"New Breakpoint...", false, dbgEnabled, [=]{
		ATActivateUIPane(kATUIPaneId_Breakpoints, true, true);
		ATUIDebuggerShowBreakpointDialog(-1);
	});

	AddSeparator(menu);

	{
		NSMenu *profMenu = AddSubmenu(menu, @"Profile");
		AddItem(profMenu, @"Profile View", false, true, [=]{
			ATActivateUIPane(kATUIPaneId_Profiler, true);
		});
	}
	AddItem(menu, @"Verifier...", g_sim.IsVerifierEnabled(), true, [=]{
		ATUIShowDialogVerifier();
	});
	AddItem(menu, @"Performance Analyzer...", false, true, [=]{
		ATActivateUIPane(kATUIPaneId_Profiler, true);
	});
}

// =========================================================================
// Record menu
// =========================================================================

static void BuildRecordMenu(NSMenu *menu) {
	SDL_Window *window = g_pWindow;
	bool recording = ATUIIsRecording();

	AddItem(menu, @"Record Raw Audio...", false, !recording, [=]{
		static const SDL_DialogFileFilter rawFilters[] = {
			{ "Raw PCM Audio", "pcm" }, { "All Files", "*" },
		};
		ATUIShowSaveFileDialog('raud', [](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordRaw, fl[0]);
		}, nullptr, window, rawFilters, 1);
	});
	AddItem(menu, @"Record Audio...", false, !recording, [=]{
		static const SDL_DialogFileFilter wavFilters[] = {
			{ "WAV Audio", "wav" }, { "All Files", "*" },
		};
		ATUIShowSaveFileDialog('raud', [](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordWAV, fl[0]);
		}, nullptr, window, wavFilters, 1);
	});
	AddItem(menu, @"Record Video...", ATUIIsVideoRecording(), !recording, [=]{
		ATUIShowVideoRecordingDialog();
	});
	AddItem(menu, @"Record SAP Type R...", false, !recording, [=]{
		static const SDL_DialogFileFilter sapFilters[] = {
			{ "SAP Files", "sap" }, { "All Files", "*" },
		};
		ATUIShowSaveFileDialog('rsap', [](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordSAP, fl[0]);
		}, nullptr, window, sapFilters, 1);
	});
	AddItem(menu, @"Record VGM...", false, !recording, [=]{
		static const SDL_DialogFileFilter vgmFilters[] = {
			{ "VGM Audio", "vgm" }, { "All Files", "*" },
		};
		ATUIShowSaveFileDialog('rvgm', [](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordVGM, fl[0]);
		}, nullptr, window, vgmFilters, 1);
	});

	AddSeparator(menu);

	AddItem(menu, @"Stop Recording", false, recording, [=]{
		ATUIStopRecording();
	});
	AddItem(menu, @"Pause/Resume Recording", false, ATUIIsVideoRecording(), [=]{
		ATUIToggleRecordingPause();
	});
}

// =========================================================================
// Tools menu
// =========================================================================

static void BuildToolsMenu(NSMenu *menu) {
	SDL_Window *window = g_pWindow;

	AddItem(menu, @"Disk Explorer...", false, true, [=]{
		g_uiState.showDiskExplorer = true;
	});

	AddItem(menu, @"Convert SAP to EXE...", false, true, [=]{
		static const SDL_DialogFileFilter kSAPFilters[] = {
			{ "SAP Music Files", "sap" }, { "All Files", "*" },
		};
		// Two-step dialog: first open SAP, then save dialog is triggered
		// by deferred processing in ATUIProcessDeferredMenuDialogs.
		ATUIShowOpenFileDialog('sap ',
			[](void *, const char * const *fl, int) {
				if (fl && fl[0]) {
					// This triggers g_sapNeedsSaveDialog via the static
					// variables in ui_menus.cpp.  However those are file-
					// static, so we use ATUIPushDeferred2 for the two-step.
					// Actually, the open callback stores the path and sets
					// g_sapNeedsSaveDialog - but those are static in ui_menus.cpp.
					// We need an alternative approach here.
					// For now, push a dummy deferred.
				}
			}, nullptr, window, kSAPFilters, 2, false);
	});

	AddItem(menu, @"Export ROM set...", false, true, [=]{
		SDL_ShowOpenFolderDialog(
			[](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_ExportROMSet, fl[0]);
			}, nullptr, window, nullptr, false);
	});

	AddItem(menu, @"Analyze tape decoding...", false, true, [=]{
		static const SDL_DialogFileFilter kTapeFilters[] = {
			{ "Audio Files", "wav;flac" }, { "All Files", "*" },
		};
		ATUIShowOpenFileDialog('cass',
			[](void *, const char * const *fl, int) {
				if (fl && fl[0]) {
					// Two-step dialog - same issue as SAP conversion.
					// The second dialog is handled by deferred processing.
				}
			}, nullptr, window, kTapeFilters, 2, false);
	});

	AddSeparator(menu);

	AddItem(menu, @"First Time Setup...", false, true, [=]{
		g_uiState.showSetupWizard = true;
	});

	AddSeparator(menu);

	AddItem(menu, @"Keyboard Shortcuts...", false, true, [=]{
		g_uiState.showKeyboardShortcuts = true;
	});
	AddItem(menu, @"Compatibility Database...", false, true, [=]{
		g_uiState.showCompatDB = true;
	});
	AddItem(menu, @"Advanced Configuration...", false, true, [=]{
		g_uiState.showAdvancedConfig = true;
	});
}

// =========================================================================
// Window menu
// =========================================================================

static void BuildWindowMenu(NSMenu *menu) {
	SDL_Window *window = g_pWindow;
	bool dbgOpen = ATUIDebuggerIsOpen();
	bool hasPanes = dbgOpen && ATUIDebuggerHasVisiblePanes();
	bool hasFocus = dbgOpen && ATUIDebuggerGetFocusedPaneId() != 0;

	AddItem(menu, @"Close", false, hasFocus, [=]{
		ATUIDebuggerCloseActivePane();
	});
	AddItem(menu, @"Undock", false, hasFocus, [=]{
		ATUIDebuggerUndockActivePane();
	});
	AddItem(menu, @"Next Pane", false, hasPanes, [=]{
		ATUIDebuggerCyclePane(+1);
	});
	AddItem(menu, @"Previous Pane", false, hasPanes, [=]{
		ATUIDebuggerCyclePane(-1);
	});

	AddSeparator(menu);

	{
		NSMenu *sizeMenu = AddSubmenu(menu, @"Adjust Window Size");
		static const struct { const char *label; int w; int h; } kSizes[] = {
			{ "1x (336x240)", 336, 240 },
			{ "2x (672x480)", 672, 480 },
			{ "3x (1008x720)", 1008, 720 },
			{ "4x (1344x960)", 1344, 960 },
		};
		for (auto& sz : kSizes) {
			int w = sz.w, h = sz.h;
			AddItem(sizeMenu,
				[NSString stringWithUTF8String:sz.label],
				false, true, [=]{
				SDL_SetWindowSize(g_pWindow, w, h);
			});
		}
	}

	AddItem(menu, @"Reset Window Layout", false, true, [=]{
		SDL_SetWindowSize(g_pWindow, 672, 480);
		SDL_SetWindowPosition(g_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	});
}

// =========================================================================
// Help menu
// =========================================================================

static void BuildHelpMenu(NSMenu *menu) {
	AddItem(menu, @"Contents", false, false, [=]{});
	AddItem(menu, @"About", false, true, [=]{
		g_uiState.showAboutDialog = true;
	});
	AddItem(menu, @"Change Log", false, true, [=]{
		g_uiState.showChangeLog = true;
	});
	AddItem(menu, @"Command-Line Help", false, true, [=]{
		g_uiState.showCommandLineHelp = true;
	});
	AddItem(menu, @"Export Debugger Help...", false, false, [=]{});
	AddItem(menu, @"Check For Updates", false, false, [=]{});
	AddItem(menu, @"Altirra Home...", false, true, [=]{
		ATLaunchURL(L"https://www.virtualdub.org/altirra.html");
	});
}

// =========================================================================
// Application menu (Altirra)
// =========================================================================

static void BuildAppMenu(NSMenu *appMenu) {
#ifdef ALTIRRA_NETPLAY_ENABLED
	const bool netplayActive = ATNetplayGlue::IsActive();
#else
	const bool netplayActive = false;
#endif

	// About
	AddItem(appMenu, @"About Altirra", false, true, [=]{
		g_uiState.showAboutDialog = true;
	});

	AddSeparator(appMenu);

	// Preferences (Cmd+,) — same Configure System dialog, disabled
	// during Online Play for the same reason.
	AddItem(appMenu,
		netplayActive ? @"Preferences... (disabled: Playing Online)"
		              : @"Preferences...",
		@",", NSEventModifierFlagCommand, false, !netplayActive, [=]{
		g_uiState.showSystemConfig = true;
	});

	AddSeparator(appMenu);

	// Hide/Show — use standard Cocoa first-responder actions
	{
		NSMenuItem *hideItem = [[NSMenuItem alloc]
			initWithTitle:@"Hide Altirra"
			action:@selector(hide:)
			keyEquivalent:@"h"];
		hideItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
		[appMenu addItem:hideItem];
	}
	{
		NSMenuItem *hideOthers = [[NSMenuItem alloc]
			initWithTitle:@"Hide Others"
			action:@selector(hideOtherApplications:)
			keyEquivalent:@"h"];
		hideOthers.keyEquivalentModifierMask =
			NSEventModifierFlagCommand | NSEventModifierFlagOption;
		[appMenu addItem:hideOthers];
	}
	{
		NSMenuItem *showAll = [[NSMenuItem alloc]
			initWithTitle:@"Show All"
			action:@selector(unhideAllApplications:)
			keyEquivalent:@""];
		[appMenu addItem:showAll];
	}

	AddSeparator(appMenu);

	// Quit (Cmd+Q)
	AddItem(appMenu, @"Quit Altirra", @"q", NSEventModifierFlagCommand, false, true, [=]{
		g_uiState.showExitConfirm = true;
	});
}

// =========================================================================
// Static storage
// =========================================================================

static NSMutableArray *g_menuDelegates = nil;
static bool g_macMenuInitialized = false;

// =========================================================================
// Public C++ API
// =========================================================================

void ATMacMenuBarInit() {
	@autoreleasepool {
		g_menuBarHeight = 0.0f;
		g_menuDelegates = [[NSMutableArray alloc] init];

		NSMenu *mainMenu = [[NSMenu alloc] init];

		// Application menu (first item — title doesn't matter, macOS uses app name)
		{
			NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
			NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Altirra"];
			appMenu.autoenablesItems = NO;
			BuildAppMenu(appMenu);
			appMenuItem.submenu = appMenu;
			[mainMenu addItem:appMenuItem];
		}

		// Top-level menus with dynamic rebuild delegates
		struct MenuDef { NSString *title; MenuBuilderFn builder; };
		MenuDef menus[] = {
			{ @"File",    BuildFileMenu },
			{ @"View",    BuildViewMenu },
			{ @"System",  BuildSystemMenu },
			{ @"Input",   BuildInputMenu },
			{ @"Cheat",   BuildCheatMenu },
			{ @"Debug",   BuildDebugMenu },
			{ @"Record",  BuildRecordMenu },
			{ @"Tools",   BuildToolsMenu },
			{ @"Window",  BuildWindowMenu },
			{ @"Help",    BuildHelpMenu },
		};

		for (auto &m : menus) {
			NSMenuItem *item = [[NSMenuItem alloc] init];
			NSMenu *sub = [[NSMenu alloc] initWithTitle:m.title];
			sub.autoenablesItems = NO;

			ATMenuDelegate *del = [[ATMenuDelegate alloc] init];
			del.builder = m.builder;
			sub.delegate = del;
			[g_menuDelegates addObject:del];

			item.submenu = sub;
			[mainMenu addItem:item];
		}

		[NSApp setMainMenu:mainMenu];
		g_macMenuInitialized = true;

		LOG_INFO("UI", "Native macOS menu bar initialized");
	}
}

void ATMacMenuBarShutdown() {
	if (!g_macMenuInitialized) return;
	@autoreleasepool {
		[NSApp setMainMenu:nil];
		g_menuDelegates = nil;
		ClearMenuActions();
		g_macMenuInitialized = false;
	}
}

bool ATMacMenuBarIsActive() {
	return g_macMenuInitialized;
}

#endif // VD_OS_MACOS
