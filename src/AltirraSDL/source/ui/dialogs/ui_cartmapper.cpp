//	AltirraSDL - Dear ImGui UI layer
//	Cartridge Mapper Selection Dialog
//	Extracted from ui_main.cpp — matches Windows IDD_CARTRIDGE_MAPPER (uicartmapper.cpp)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <at/atio/cartridgetypes.h>

#include "ui_main.h"
#include "cartridge.h"
#include "uitypes.h"

#include <algorithm>

// Forward declaration for cartridge autodetect (defined in cartdetect.cpp)
uint32 ATCartridgeAutodetectMode(const void *data, uint32 size, vdfastvector<int>& cartModes);

// Deferred action support — defined in ui_main.cpp
extern void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra);

// =========================================================================
// Cartridge Mapper Selection Dialog state
//
// When a cartridge with unknown mapper is loaded, the image data is captured
// here and the dialog is shown.  The user selects a mapper type, and the
// cartridge is re-loaded with the chosen mapper on confirmation.
// =========================================================================

struct ATCartridgeMapperDialogState {
	bool initialized = false;
	VDStringW pendingPath;
	ATDeferredActionType originalAction = kATDeferred_AttachCartridge;
	int cartSlot = 0;                   // 0 = primary, 1 = secondary
	bool coldResetAfterLoad = true;
	vdfastvector<uint8> capturedData;
	uint32 cartSize = 0;

	// Mapper lists
	vdfastvector<int> originalMappers;   // auto-detected mappers (recommended first)
	vdfastvector<int> displayMappers;    // currently displayed (may include all)
	uint32 recommendedCount = 0;         // how many of originalMappers are recommended

	int selectedIndex = 0;
	bool showAll = false;
	bool showDetails = false;
	bool show2600Warning = false;
};

static ATCartridgeMapperDialogState g_cartMapperState;

// Cartridge mapper mode name — matches Windows uicartmapper.cpp GetModeName()
static const char *ATUIGetCartridgeModeName(int mode) {
	switch(mode) {
		case kATCartridgeMode_8K:                    return "8K";
		case kATCartridgeMode_16K:                   return "16K";
		case kATCartridgeMode_OSS_034M:              return "OSS '034M'";
		case kATCartridgeMode_5200_32K:              return "5200 32K";
		case kATCartridgeMode_DB_32K:                return "DB 32K";
		case kATCartridgeMode_5200_16K_TwoChip:      return "5200 16K (two chip)";
		case kATCartridgeMode_BountyBob5200:         return "Bounty Bob (5200)";
		case kATCartridgeMode_Williams_64K:          return "Williams 64K";
		case kATCartridgeMode_Express_64K:           return "Express 64K";
		case kATCartridgeMode_Diamond_64K:           return "Diamond 64K";
		case kATCartridgeMode_SpartaDosX_64K:        return "SpartaDOS X 64K";
		case kATCartridgeMode_XEGS_32K:              return "32K XEGS";
		case kATCartridgeMode_XEGS_64K:              return "64K XEGS";
		case kATCartridgeMode_XEGS_128K:             return "128K XEGS";
		case kATCartridgeMode_OSS_M091:              return "OSS 'M091'";
		case kATCartridgeMode_5200_16K_OneChip:      return "5200 16K (one chip)";
		case kATCartridgeMode_Atrax_128K:            return "Atrax 128K (decoded order)";
		case kATCartridgeMode_BountyBob800:          return "Bounty Bob (800)";
		case kATCartridgeMode_5200_8K:               return "5200 8K";
		case kATCartridgeMode_5200_4K:               return "5200 4K";
		case kATCartridgeMode_RightSlot_8K:          return "Right slot 8K";
		case kATCartridgeMode_Williams_32K:          return "Williams 32K";
		case kATCartridgeMode_XEGS_256K:             return "256K XEGS";
		case kATCartridgeMode_XEGS_512K:             return "512K XEGS";
		case kATCartridgeMode_XEGS_1M:               return "1M XEGS";
		case kATCartridgeMode_MegaCart_16K:           return "16K MegaCart";
		case kATCartridgeMode_MegaCart_32K:           return "32K MegaCart";
		case kATCartridgeMode_MegaCart_64K:           return "64K MegaCart";
		case kATCartridgeMode_MegaCart_128K:          return "128K MegaCart";
		case kATCartridgeMode_MegaCart_256K:          return "256K MegaCart";
		case kATCartridgeMode_MegaCart_512K:          return "512K MegaCart";
		case kATCartridgeMode_MegaCart_1M:            return "1M MegaCart";
		case kATCartridgeMode_MegaCart_2M:            return "2M MegaCart";
		case kATCartridgeMode_Switchable_XEGS_32K:   return "32K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_64K:   return "64K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_128K:  return "128K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_256K:  return "256K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_512K:  return "512K Switchable XEGS";
		case kATCartridgeMode_Switchable_XEGS_1M:    return "1M Switchable XEGS";
		case kATCartridgeMode_Phoenix_8K:            return "Phoenix 8K";
		case kATCartridgeMode_Blizzard_16K:          return "Blizzard 16K";
		case kATCartridgeMode_MaxFlash_128K:         return "MaxFlash 128K / 1Mbit";
		case kATCartridgeMode_MaxFlash_1024K:        return "MaxFlash 1M / 8Mbit - older (bank 127)";
		case kATCartridgeMode_SpartaDosX_128K:       return "SpartaDOS X 128K";
		case kATCartridgeMode_OSS_8K:                return "OSS 8K";
		case kATCartridgeMode_OSS_043M:              return "OSS '043M'";
		case kATCartridgeMode_Blizzard_4K:           return "Blizzard 4K";
		case kATCartridgeMode_AST_32K:               return "AST 32K";
		case kATCartridgeMode_Atrax_SDX_64K:         return "Atrax SDX 64K";
		case kATCartridgeMode_Atrax_SDX_128K:        return "Atrax SDX 128K";
		case kATCartridgeMode_Turbosoft_64K:         return "Turbosoft 64K";
		case kATCartridgeMode_Turbosoft_128K:        return "Turbosoft 128K";
		case kATCartridgeMode_MaxFlash_128K_MyIDE:   return "MaxFlash 128K + MyIDE";
		case kATCartridgeMode_Corina_1M_EEPROM:      return "Corina 1M + 8K EEPROM";
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM: return "Corina 512K + 512K SRAM + 8K EEPROM";
		case kATCartridgeMode_TelelinkII:            return "8K Telelink II";
		case kATCartridgeMode_SIC_128K:              return "SIC! 128K";
		case kATCartridgeMode_SIC_256K:              return "SIC! 256K";
		case kATCartridgeMode_SIC_512K:              return "SIC! 512K";
		case kATCartridgeMode_MaxFlash_1024K_Bank0:  return "MaxFlash 1M / 8Mbit - newer (bank 0)";
		case kATCartridgeMode_MegaCart_1M_2:         return "Megacart 1M (2)";
		case kATCartridgeMode_5200_64K_32KBanks:     return "5200 64K Super Cart (32K banks)";
		case kATCartridgeMode_5200_128K_32KBanks:    return "5200 128K Super Cart (32K banks)";
		case kATCartridgeMode_5200_256K_32KBanks:    return "5200 256K Super Cart (32K banks)";
		case kATCartridgeMode_5200_512K_32KBanks:    return "5200 512K Super Cart (32K banks)";
		case kATCartridgeMode_MicroCalc:             return "MicroCalc 32K";
		case kATCartridgeMode_2K:                    return "2K";
		case kATCartridgeMode_4K:                    return "4K";
		case kATCartridgeMode_RightSlot_4K:          return "Right slot 4K";
		case kATCartridgeMode_Blizzard_32K:          return "Blizzard 32K";
		case kATCartridgeMode_MegaCart_512K_3:       return "MegaCart 512K (3)";
		case kATCartridgeMode_MegaMax_2M:            return "MegaMax 2M";
		case kATCartridgeMode_TheCart_128M:           return "The!Cart 128M";
		case kATCartridgeMode_MegaCart_4M_3:         return "MegaCart 4M (3)";
		case kATCartridgeMode_TheCart_32M:            return "The!Cart 32M";
		case kATCartridgeMode_TheCart_64M:            return "The!Cart 64M";
		case kATCartridgeMode_BountyBob5200Alt:      return "Bounty Bob (5200) - Alternate layout";
		case kATCartridgeMode_XEGS_64K_Alt:          return "XEGS 64K (alternate)";
		case kATCartridgeMode_Atrax_128K_Raw:        return "Atrax 128K (raw order)";
		case kATCartridgeMode_aDawliah_32K:          return "aDawliah 32K";
		case kATCartridgeMode_aDawliah_64K:          return "aDawliah 64K";
		case kATCartridgeMode_JRC6_64K:              return "JRC 64K";
		case kATCartridgeMode_JRC_RAMBOX:            return "JRC RAMBOX";
		case kATCartridgeMode_XEMulticart_8K:        return "XE Multicart (8K)";
		case kATCartridgeMode_XEMulticart_16K:       return "XE Multicart (16K)";
		case kATCartridgeMode_XEMulticart_32K:       return "XE Multicart (32K)";
		case kATCartridgeMode_XEMulticart_64K:       return "XE Multicart (64K)";
		case kATCartridgeMode_XEMulticart_128K:      return "XE Multicart (128K)";
		case kATCartridgeMode_XEMulticart_256K:      return "XE Multicart (256K)";
		case kATCartridgeMode_XEMulticart_512K:      return "XE Multicart (512K)";
		case kATCartridgeMode_XEMulticart_1M:        return "XE Multicart (1MB)";
		case kATCartridgeMode_SICPlus:               return "SIC+";
		case kATCartridgeMode_Williams_16K:          return "Williams 16K";
		case kATCartridgeMode_MDDOS:                 return "MDDOS";
		case kATCartridgeMode_COS32K:                return "COS 32K";
		case kATCartridgeMode_Pronto:                return "Pronto";
		case kATCartridgeMode_JAtariCart_8K:          return "J(atari)Cart 8K";
		case kATCartridgeMode_JAtariCart_16K:         return "J(atari)Cart 16K";
		case kATCartridgeMode_JAtariCart_32K:         return "J(atari)Cart 32K";
		case kATCartridgeMode_JAtariCart_64K:         return "J(atari)Cart 64K";
		case kATCartridgeMode_JAtariCart_128K:        return "J(atari)Cart 128K";
		case kATCartridgeMode_JAtariCart_256K:        return "J(atari)Cart 256K";
		case kATCartridgeMode_JAtariCart_512K:        return "J(atari)Cart 512K";
		case kATCartridgeMode_JAtariCart_1024K:       return "J(atari)Cart 1MB";
		case kATCartridgeMode_DCart:                  return "DCart";
		default:                                     return "Unknown";
	}
}

// Cartridge mapper mode description — matches Windows uicartmapper.cpp GetModeDesc()
static const char *ATUIGetCartridgeModeDesc(int mode) {
	switch(mode) {
		case kATCartridgeMode_8K:                    return "8K fixed";
		case kATCartridgeMode_16K:                   return "16K fixed";
		case kATCartridgeMode_OSS_034M:              return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_5200_32K:              return "32K fixed";
		case kATCartridgeMode_DB_32K:                return "8K banked by CCTL address + 8K fixed";
		case kATCartridgeMode_5200_16K_TwoChip:      return "16K fixed";
		case kATCartridgeMode_BountyBob800:
		case kATCartridgeMode_BountyBob5200:
		case kATCartridgeMode_BountyBob5200Alt:      return "4K+4K banked by $4/5FF6-9 + 8K fixed";
		case kATCartridgeMode_Williams_64K:
		case kATCartridgeMode_Williams_32K:
		case kATCartridgeMode_Williams_16K:           return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_Express_64K:           return "8K banked by CCTL $D57x (switchable)";
		case kATCartridgeMode_Diamond_64K:           return "8K banked by CCTL $D5Dx (switchable)";
		case kATCartridgeMode_Atrax_SDX_64K:
		case kATCartridgeMode_SpartaDosX_64K:        return "8K banked by CCTL $D5Ex (switchable)";
		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_XEGS_64K_Alt:
		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_XEGS_256K:
		case kATCartridgeMode_XEGS_512K:
		case kATCartridgeMode_XEGS_1M:               return "8K banked by CCTL data + 8K fixed (switchable)";
		case kATCartridgeMode_OSS_M091:              return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_5200_16K_OneChip:      return "16K fixed";
		case kATCartridgeMode_Atrax_128K:
		case kATCartridgeMode_Atrax_128K_Raw:        return "8K banked by CCTL data (switchable)";
		case kATCartridgeMode_5200_8K:               return "8K fixed";
		case kATCartridgeMode_5200_4K:               return "4K fixed";
		case kATCartridgeMode_RightSlot_8K:          return "8K right slot fixed";
		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
		case kATCartridgeMode_MegaCart_2M:            return "16K banked by CCTL data (switchable)";
		case kATCartridgeMode_Switchable_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_256K:
		case kATCartridgeMode_Switchable_XEGS_512K:
		case kATCartridgeMode_Switchable_XEGS_1M:    return "8K banked by CCTL data + 8K fixed (switchable)";
		case kATCartridgeMode_Phoenix_8K:            return "8K fixed (one-time disable)";
		case kATCartridgeMode_Blizzard_4K:           return "8K fixed (one-time disable)";
		case kATCartridgeMode_Blizzard_16K:          return "16K fixed (one-time disable)";
		case kATCartridgeMode_Blizzard_32K:          return "8K banked (autoincrement + disable)";
		case kATCartridgeMode_MaxFlash_128K:         return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_MaxFlash_1024K:        return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_MaxFlash_1024K_Bank0:  return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_MaxFlash_128K_MyIDE:   return "8K banked + CCTL keyhole (switchable)";
		case kATCartridgeMode_Atrax_SDX_128K:
		case kATCartridgeMode_SpartaDosX_128K:       return "8K banked by CCTL $D5E0-D5FF address (switchable)";
		case kATCartridgeMode_OSS_8K:                return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_OSS_043M:              return "4K banked by CCTL data + 4K fixed";
		case kATCartridgeMode_AST_32K:               return "8K disableable + CCTL autoincrement by write";
		case kATCartridgeMode_Turbosoft_64K:
		case kATCartridgeMode_Turbosoft_128K:        return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_Corina_1M_EEPROM:
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM: return "8K+8K banked (complex)";
		case kATCartridgeMode_TelelinkII:            return "8K fixed + EEPROM";
		case kATCartridgeMode_SIC_128K:
		case kATCartridgeMode_SIC_256K:
		case kATCartridgeMode_SIC_512K:              return "16K banked by CCTL $D500-D51F access (8K+8K switchable)";
		case kATCartridgeMode_MegaCart_1M_2:         return "8K banked by CCTL data (switchable)";
		case kATCartridgeMode_5200_64K_32KBanks:     return "32K banked by $BFD0-BFFF access";
		case kATCartridgeMode_5200_128K_32KBanks:    return "32K banked by $BFD0-BFFF access";
		case kATCartridgeMode_5200_256K_32KBanks:    return "32K banked by $BFC0-BFFF access";
		case kATCartridgeMode_5200_512K_32KBanks:    return "32K banked by $BFC0-BFFF access";
		case kATCartridgeMode_MicroCalc:             return "8K banked by CCTL access (autoincrement, switchable)";
		case kATCartridgeMode_2K:                    return "2K fixed";
		case kATCartridgeMode_4K:                    return "4K fixed";
		case kATCartridgeMode_RightSlot_4K:          return "4K fixed right slot";
		case kATCartridgeMode_MegaCart_512K_3:       return "16K banked by CCTL data (switchable)";
		case kATCartridgeMode_MegaMax_2M:            return "16K banked by CCTL address (switchable)";
		case kATCartridgeMode_MegaCart_4M_3:         return "16K banked by CCTL data (switchable)";
		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:           return "8K+8K banked (complex)";
		case kATCartridgeMode_aDawliah_32K:          return "8K banked by CCTL access (autoincrement)";
		case kATCartridgeMode_aDawliah_64K:          return "8K banked by CCTL access (autoincrement)";
		case kATCartridgeMode_JRC6_64K:              return "8K banked by CCTL $D500-D57F data (switchable)";
		case kATCartridgeMode_JRC_RAMBOX:            return "8K banked by CCTL $D500-D57F data (switchable) + RAM";
		case kATCartridgeMode_XEMulticart_8K:
		case kATCartridgeMode_XEMulticart_16K:
		case kATCartridgeMode_XEMulticart_32K:
		case kATCartridgeMode_XEMulticart_64K:
		case kATCartridgeMode_XEMulticart_128K:
		case kATCartridgeMode_XEMulticart_256K:
		case kATCartridgeMode_XEMulticart_512K:
		case kATCartridgeMode_XEMulticart_1M:        return "8K or 16K banked by CCTL write";
		case kATCartridgeMode_SICPlus:               return "16K banked by CCTL $D500-D51F access (8K+8K switchable)";
		case kATCartridgeMode_MDDOS:                 return "4K banked by CCTL access (4K+4K switchable)";
		case kATCartridgeMode_COS32K:                return "16K banked by CCTL access";
		case kATCartridgeMode_Pronto:                return "16K fixed + EEPROM";
		case kATCartridgeMode_JAtariCart_8K:
		case kATCartridgeMode_JAtariCart_16K:
		case kATCartridgeMode_JAtariCart_32K:
		case kATCartridgeMode_JAtariCart_64K:
		case kATCartridgeMode_JAtariCart_128K:
		case kATCartridgeMode_JAtariCart_256K:
		case kATCartridgeMode_JAtariCart_512K:
		case kATCartridgeMode_JAtariCart_1024K:       return "8K banked by CCTL address (switchable)";
		case kATCartridgeMode_DCart:                  return "8K banked by CCTL write (switchable) + keyhole";
		default:                                     return "";
	}
}

// Pending flag — set by deferred actions, consumed by render loop
bool g_cartMapperPending = false;

// Open the cartridge mapper selection dialog for a cartridge with unknown mapper.
// Populates g_cartMapperState; render loop picks up g_cartMapperPending.
void ATUIOpenCartridgeMapperDialog(ATDeferredActionType origAction,
	const VDStringW &path, int slot, bool coldReset,
	const vdfastvector<uint8> &capturedData, uint32 cartSize)
{
	auto &s = g_cartMapperState;
	s.pendingPath = path;
	s.originalAction = origAction;
	s.cartSlot = slot;
	s.coldResetAfterLoad = coldReset;
	s.capturedData = capturedData;
	s.cartSize = cartSize;
	s.selectedIndex = 0;
	s.showAll = false;
	s.showDetails = false;
	s.initialized = false;

	// Detect 2600 cartridge (NMI/RESET/IRQ vectors in Fxxx range)
	s.show2600Warning = false;
	if ((cartSize == 2048 || cartSize == 4096) && !capturedData.empty()) {
		const uint8 *tail = capturedData.data() + capturedData.size() - 6;
		if (tail[1] >= 0xF0 && tail[3] >= 0xF0 && tail[5] >= 0xF0)
			s.show2600Warning = true;
	}

	// Run autodetect to get recommended mappers
	s.originalMappers.clear();
	s.recommendedCount = 0;
	if (!capturedData.empty()) {
		s.recommendedCount = ATCartridgeAutodetectMode(
			capturedData.data(), cartSize, s.originalMappers);
	}

	g_cartMapperPending = true;
}

// =========================================================================
// Cartridge Mapper Selection dialog
// Matches Windows IDD_CARTRIDGE_MAPPER (uicartmapper.cpp)
// =========================================================================

static void ATUIBuildCartridgeMapperList() {
	auto &s = g_cartMapperState;
	s.displayMappers = s.originalMappers;

	if (s.showAll) {
		vdfastvector<bool> seen(kATCartridgeModeCount, false);

		// Skip invisible mappers
		seen[kATCartridgeMode_None] = true;
		seen[kATCartridgeMode_SuperCharger3D] = true;

		for (int m : s.displayMappers) {
			if (m >= 0 && m < (int)kATCartridgeModeCount)
				seen[m] = true;
		}

		// Add all mappers that have a CAR mapping, in mapper number order
		for (int i = 0; i <= (int)kATCartridgeMapper_Max; ++i) {
			ATCartridgeMode mode = ATGetCartridgeModeForMapper(i);
			if (mode != kATCartridgeMode_None && !seen[mode]) {
				seen[mode] = true;
				s.displayMappers.push_back(mode);
			}
		}

		// Add anything else selectable
		for (int i = 1; i < (int)kATCartridgeModeCount; ++i) {
			if (!seen[i])
				s.displayMappers.push_back(i);
		}
	}

	// Sort: recommended mappers first (by mapper number), then same-system-type,
	// then the rest — matching Windows sorting logic
	auto mapSorter = [](int x, int y) -> bool {
		int xm = ATGetCartridgeMapperForMode((ATCartridgeMode)x, 0);
		int ym = ATGetCartridgeMapperForMode((ATCartridgeMode)y, 0);
		if (!xm) xm = 1000;
		if (!ym) ym = 1000;
		if (xm < ym) return true;
		if (xm == ym && xm == 1000 &&
			strcmp(ATUIGetCartridgeModeName(x), ATUIGetCartridgeModeName(y)) < 0)
			return true;
		return false;
	};

	auto it0 = s.displayMappers.begin();
	auto it1 = it0 + std::min((uint32)s.displayMappers.size(), s.recommendedCount);
	auto it2 = it1;
	auto it3 = s.displayMappers.end();

	std::sort(it0, it1, mapSorter);

	// Bubble same-system-type non-recommended items to the top
	if (s.recommendedCount && it0 != it1) {
		bool firstIs5200 = ATIsCartridge5200Mode((ATCartridgeMode)*it0);
		bool allSameType = std::find_if(it0 + 1, it1,
			[=](int key) { return firstIs5200 != ATIsCartridge5200Mode((ATCartridgeMode)key); }) == it1;

		if (allSameType) {
			it2 = std::partition(it1, it3,
				[=](int key) { return firstIs5200 == ATIsCartridge5200Mode((ATCartridgeMode)key); });
		}
	}

	std::sort(it1, it2, mapSorter);
	std::sort(it2, it3, mapSorter);

	s.selectedIndex = s.displayMappers.empty() ? -1 : 0;
}

void ATUIRenderCartridgeMapper(ATUIState &state) {
	if (!state.showCartridgeMapper)
		return;

	auto &s = g_cartMapperState;

	if (!s.initialized) {
		ATUIBuildCartridgeMapperList();
		s.initialized = true;
	}

	ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Select Cartridge Mapper", &state.showCartridgeMapper,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showCartridgeMapper = false;
		ImGui::End();
		return;
	}

	if (s.show2600Warning) {
		ImGui::PushStyleColor(ImGuiCol_Text, ATUIColorWarningText());
		ImGui::TextWrapped("Warning: This image appears to be an Atari 2600 cartridge. "
			"Altirra is an Atari 800/5200 emulator and cannot run 2600 games.");
		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	ImGui::Text("Image size: %u bytes  |  %zu mapper(s) detected",
		s.cartSize, s.originalMappers.size());
	ImGui::Spacing();

	if (s.displayMappers.empty()) {
		ImGui::TextWrapped("No compatible mappers found for this image size.");
	} else {
		// Mapper table
		float tableHeight = ImGui::GetContentRegionAvail().y - 36.0f;
		int numCols = s.showDetails ? 3 : 2;

		if (ImGui::BeginTable("MapperList", numCols,
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_Resizable,
				ImVec2(0, tableHeight))) {

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			if (s.showDetails)
				ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)s.displayMappers.size(); ++i) {
				int mode = s.displayMappers[i];
				ImGui::TableNextRow();

				bool isRecommended = (i == 0 && s.recommendedCount == 1);
				bool isSuggested = (i < (int)s.recommendedCount && s.recommendedCount > 1);

				// Column 0: mapper number
				ImGui::TableNextColumn();
				int mapperNum = ATGetCartridgeMapperForMode((ATCartridgeMode)mode, s.cartSize);
				if (mapperNum)
					ImGui::Text("%d", mapperNum);

				// Column 1: name (with selectable row)
				ImGui::TableNextColumn();
				char label[256];
				const char *modeName = ATUIGetCartridgeModeName(mode);
				if (isSuggested)
					snprintf(label, sizeof(label), "*%s##m%d", modeName, mode);
				else if (isRecommended)
					snprintf(label, sizeof(label), "%s (recommended)##m%d", modeName, mode);
				else
					snprintf(label, sizeof(label), "%s##m%d", modeName, mode);

				bool selected = (i == s.selectedIndex);
				if (ImGui::Selectable(label, selected,
						ImGuiSelectableFlags_SpanAllColumns |
						ImGuiSelectableFlags_AllowDoubleClick)) {
					s.selectedIndex = i;
					if (ImGui::IsMouseDoubleClicked(0)) {
						// Double-click = accept
						int chosenMode = s.displayMappers[s.selectedIndex];
						ATUIPushDeferred(s.originalAction,
							VDTextWToU8(s.pendingPath).c_str(), chosenMode);
						state.showCartridgeMapper = false;
					}
				}

				// Column 2: details (optional)
				if (s.showDetails) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(ATUIGetCartridgeModeDesc(mode));
				}
			}

			ImGui::EndTable();
		}
	}

	// Bottom buttons
	bool showAllChanged = ImGui::Checkbox("Show All", &s.showAll);
	ImGui::SameLine();
	bool showDetailsChanged = ImGui::Checkbox("Show Details", &s.showDetails);

	if (showAllChanged || showDetailsChanged) {
		ATUIBuildCartridgeMapperList();
	}

	ImGui::SameLine();
	float buttonWidth = 80.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * buttonWidth - spacing - ImGui::GetStyle().WindowPadding.x);

	bool canOK = (s.selectedIndex >= 0 && s.selectedIndex < (int)s.displayMappers.size());
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)) && canOK) {
		int chosenMode = s.displayMappers[s.selectedIndex];
		ATUIPushDeferred(s.originalAction,
			VDTextWToU8(s.pendingPath).c_str(), chosenMode);
		state.showCartridgeMapper = false;
	}

	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
		state.showCartridgeMapper = false;

	ImGui::End();
}
