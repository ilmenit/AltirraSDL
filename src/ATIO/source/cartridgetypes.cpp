//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - cartridge type utils
//	Copyright (C) 2009-2016 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <at/atcore/configvar.h>
#include <at/atio/cartridgetypes.h>

ATConfigVarInt32 g_ATCVCartType_MaxFlash128KMyIDE("cart.cartype.maxflash128k_myide", 0);
ATConfigVarInt32 g_ATCVCartType_BountyBob5200Alt("cart.cartype.bb5200_alt", 0);

ATCartridgeMode ATGetCartridgeModeForMapper(int mapper) {
	if (mapper) {
		if (mapper == g_ATCVCartType_MaxFlash128KMyIDE	) return kATCartridgeMode_MaxFlash_128K_MyIDE;
		if (mapper == g_ATCVCartType_BountyBob5200Alt	) return kATCartridgeMode_BountyBob5200Alt;
	}

	switch(mapper) {
		case 1 : return kATCartridgeMode_8K;
		case 2 : return kATCartridgeMode_16K;
		case 3 : return kATCartridgeMode_OSS_034M;
		case 4 : return kATCartridgeMode_5200_32K;
		case 5 : return kATCartridgeMode_DB_32K;
		case 6 : return kATCartridgeMode_5200_16K_TwoChip;
		case 7 : return kATCartridgeMode_BountyBob5200;
		case 8 : return kATCartridgeMode_Williams_64K;
		case 9 : return kATCartridgeMode_Express_64K;
		case 10: return kATCartridgeMode_Diamond_64K;
		case 11: return kATCartridgeMode_SpartaDosX_64K;
		case 12: return kATCartridgeMode_XEGS_32K;
		case 13: return kATCartridgeMode_XEGS_64K;
		case 14: return kATCartridgeMode_XEGS_128K;
		case 15: return kATCartridgeMode_OSS_M091;
		case 16: return kATCartridgeMode_5200_16K_OneChip;
		case 17: return kATCartridgeMode_Atrax_128K;
		case 18: return kATCartridgeMode_BountyBob800;
		case 19: return kATCartridgeMode_5200_8K;
		case 20: return kATCartridgeMode_5200_4K;
		case 21: return kATCartridgeMode_RightSlot_8K;
		case 22: return kATCartridgeMode_Williams_32K;
		case 23: return kATCartridgeMode_XEGS_256K;
		case 24: return kATCartridgeMode_XEGS_512K;
		case 25: return kATCartridgeMode_XEGS_1M;
		case 26: return kATCartridgeMode_MegaCart_16K;
		case 27: return kATCartridgeMode_MegaCart_32K;
		case 28: return kATCartridgeMode_MegaCart_64K;
		case 29: return kATCartridgeMode_MegaCart_128K;
		case 30: return kATCartridgeMode_MegaCart_256K;
		case 31: return kATCartridgeMode_MegaCart_512K;
		case 32: return kATCartridgeMode_MegaCart_1M;
		case 33: return kATCartridgeMode_Switchable_XEGS_32K;
		case 34: return kATCartridgeMode_Switchable_XEGS_64K;
		case 35: return kATCartridgeMode_Switchable_XEGS_128K;
		case 36: return kATCartridgeMode_Switchable_XEGS_256K;
		case 37: return kATCartridgeMode_Switchable_XEGS_512K;
		case 38: return kATCartridgeMode_Switchable_XEGS_1M;
		case 39: return kATCartridgeMode_Phoenix_8K;
		case 40: return kATCartridgeMode_Blizzard_16K;
		case 41: return kATCartridgeMode_MaxFlash_128K;
		case 42: return kATCartridgeMode_MaxFlash_1024K;
		case 43: return kATCartridgeMode_SpartaDosX_128K;
		case 44: return kATCartridgeMode_OSS_8K;
		case 45: return kATCartridgeMode_OSS_043M;
		case 46: return kATCartridgeMode_Blizzard_4K;
		case 47: return kATCartridgeMode_AST_32K;
		case 48: return kATCartridgeMode_Atrax_SDX_64K;
		case 49: return kATCartridgeMode_Atrax_SDX_128K;
		case 50: return kATCartridgeMode_Turbosoft_64K;
		case 51: return kATCartridgeMode_Turbosoft_128K;
		case 52: return kATCartridgeMode_MicroCalc;
		case 53: return kATCartridgeMode_RightSlot_8K;
		case 54: return kATCartridgeMode_SIC_128K;
		case 55: return kATCartridgeMode_SIC_256K;
		case 56: return kATCartridgeMode_SIC_512K;
		case 57: return kATCartridgeMode_2K;
		case 58: return kATCartridgeMode_4K;
		case 59: return kATCartridgeMode_RightSlot_4K;
		case 60: return kATCartridgeMode_Blizzard_32K;
		case 61: return kATCartridgeMode_MegaMax_2M;
		case 62: return kATCartridgeMode_TheCart_128M;
		case 63: return kATCartridgeMode_MegaCart_4M_3;
		case 64: return kATCartridgeMode_MegaCart_2M;
		case 65: return kATCartridgeMode_TheCart_32M;
		case 66: return kATCartridgeMode_TheCart_64M;
		case 67: return kATCartridgeMode_XEGS_64K_Alt;
		case 68: return kATCartridgeMode_Atrax_128K_Raw;
		case 69: return kATCartridgeMode_aDawliah_32K;
		case 70: return kATCartridgeMode_aDawliah_64K;
		case 71: return kATCartridgeMode_5200_64K_32KBanks;
		case 72: return kATCartridgeMode_5200_128K_32KBanks;
		case 73: return kATCartridgeMode_5200_256K_32KBanks;
		case 74: return kATCartridgeMode_5200_512K_32KBanks;
		case 75: return kATCartridgeMode_MaxFlash_1024K_Bank0;
		case 76: return kATCartridgeMode_Williams_16K;
		case 78: return kATCartridgeMode_TelelinkII;
		case 79: return kATCartridgeMode_Pronto;
		case 80: return kATCartridgeMode_JRC_RAMBOX;
		case 81: return kATCartridgeMode_MDDOS;
		case 82: return kATCartridgeMode_COS32K;
		case 83: return kATCartridgeMode_SICPlus;
		case 84: return kATCartridgeMode_Corina_1M_EEPROM;
		case 85: return kATCartridgeMode_Corina_512K_SRAM_EEPROM;
		case 86: return kATCartridgeMode_XEMulticart_8K;
		case 87: return kATCartridgeMode_XEMulticart_16K;
		case 88: return kATCartridgeMode_XEMulticart_32K;
		case 89: return kATCartridgeMode_XEMulticart_64K;
		case 90: return kATCartridgeMode_XEMulticart_128K;
		case 91: return kATCartridgeMode_XEMulticart_256K;
		case 92: return kATCartridgeMode_XEMulticart_512K;
		case 93: return kATCartridgeMode_XEMulticart_1M;

		case 94: return kATCartridgeMode_RamCart_64K;
		case 95: return kATCartridgeMode_RamCart_128K;
		case 96: return kATCartridgeMode_DoubleRamCart_256K;
		case 97: return kATCartridgeMode_RamCart_1M;
		case 98: return kATCartridgeMode_RamCart_2M;
		case 99: return kATCartridgeMode_RamCart_4M;
		case 100: return kATCartridgeMode_RamCart_8M;
		case 101: return kATCartridgeMode_RamCart_16M;
		case 102: return kATCartridgeMode_RamCart_32M;
		case 103: return kATCartridgeMode_SiDiCar_32K;

		case 104: return kATCartridgeMode_JAtariCart_8K;
		case 105: return kATCartridgeMode_JAtariCart_16K;
		case 106: return kATCartridgeMode_JAtariCart_32K;
		case 107: return kATCartridgeMode_JAtariCart_64K;
		case 108: return kATCartridgeMode_JAtariCart_128K;
		case 109: return kATCartridgeMode_JAtariCart_256K;
		case 110: return kATCartridgeMode_JAtariCart_512K;
		case 111: return kATCartridgeMode_JAtariCart_1024K;
		case 112: return kATCartridgeMode_DCart;

		case 160: return kATCartridgeMode_JRC6_64K;
		default:
			return kATCartridgeMode_None;
	}
}

int ATGetCartridgeMapperForMode(ATCartridgeMode mode, uint32 size) {
	switch(mode) {
		case kATCartridgeMode_8K: return 1;
		case kATCartridgeMode_16K: return 2;
		case kATCartridgeMode_OSS_034M: return 3;
		case kATCartridgeMode_5200_32K: return 4;
		case kATCartridgeMode_DB_32K: return 5;
		case kATCartridgeMode_5200_16K_TwoChip: return 6;
		case kATCartridgeMode_BountyBob5200: return 7;
		case kATCartridgeMode_Williams_64K: return 8;
		case kATCartridgeMode_Express_64K: return 9;
		case kATCartridgeMode_Diamond_64K: return 10;
		case kATCartridgeMode_SpartaDosX_64K: return 11;
		case kATCartridgeMode_XEGS_32K: return 12;
		case kATCartridgeMode_XEGS_64K: return 13;
		case kATCartridgeMode_XEGS_128K: return 14;
		case kATCartridgeMode_OSS_M091: return 15;
		case kATCartridgeMode_5200_16K_OneChip: return 16;
		case kATCartridgeMode_Atrax_128K: return 17;
		case kATCartridgeMode_BountyBob800: return 18;
		case kATCartridgeMode_5200_8K: return 19;
		case kATCartridgeMode_5200_4K: return 20;
		case kATCartridgeMode_RightSlot_8K: return 21;
		case kATCartridgeMode_Williams_32K: return 22;
		case kATCartridgeMode_XEGS_256K: return 23;
		case kATCartridgeMode_XEGS_512K: return 24;
		case kATCartridgeMode_XEGS_1M: return 25;
		case kATCartridgeMode_MegaCart_16K: return 26;
		case kATCartridgeMode_MegaCart_32K: return 27;
		case kATCartridgeMode_MegaCart_64K: return 28;
		case kATCartridgeMode_MegaCart_128K: return 29;
		case kATCartridgeMode_MegaCart_256K: return 30;
		case kATCartridgeMode_MegaCart_512K: return 31;
		case kATCartridgeMode_MegaCart_1M: return 32;
		case kATCartridgeMode_Switchable_XEGS_32K: return 33;
		case kATCartridgeMode_Switchable_XEGS_64K: return 34;
		case kATCartridgeMode_Switchable_XEGS_128K: return 35;
		case kATCartridgeMode_Switchable_XEGS_256K: return 36;
		case kATCartridgeMode_Switchable_XEGS_512K: return 37;
		case kATCartridgeMode_Switchable_XEGS_1M: return 38;
		case kATCartridgeMode_Phoenix_8K: return 39;
		case kATCartridgeMode_Blizzard_16K: return 40;
		case kATCartridgeMode_MaxFlash_128K: return 41;
		case kATCartridgeMode_MaxFlash_1024K: return 42;
		case kATCartridgeMode_SpartaDosX_128K: return 43;
		case kATCartridgeMode_OSS_8K: return 44;
		case kATCartridgeMode_OSS_043M: return 45;
		case kATCartridgeMode_Blizzard_4K: return 46;
		case kATCartridgeMode_AST_32K: return 47;
		case kATCartridgeMode_Atrax_SDX_64K: return 48;
		case kATCartridgeMode_Atrax_SDX_128K: return 49;
		case kATCartridgeMode_Turbosoft_64K: return 50;
		case kATCartridgeMode_Turbosoft_128K: return 51;
		case kATCartridgeMode_MicroCalc: return 52;

		case kATCartridgeMode_SIC_128K: return 54;
		case kATCartridgeMode_SIC_256K: return 55;
		case kATCartridgeMode_SIC_512K: return 56;

		case kATCartridgeMode_2K:
			return 57;

		case kATCartridgeMode_4K:
			return 58;

		case kATCartridgeMode_RightSlot_4K:
			return 59;

		case kATCartridgeMode_Blizzard_32K:		return 60;
		case kATCartridgeMode_MegaMax_2M:		return 61;
		case kATCartridgeMode_TheCart_128M:		return 62;
		case kATCartridgeMode_MegaCart_4M_3:	return 63;
		case kATCartridgeMode_MegaCart_2M:		return 64;
		case kATCartridgeMode_TheCart_32M:		return 65;
		case kATCartridgeMode_TheCart_64M:		return 66;
		case kATCartridgeMode_XEGS_64K_Alt:		return 67;
		case kATCartridgeMode_Atrax_128K_Raw:	return 68;
		case kATCartridgeMode_aDawliah_32K:		return 69;
		case kATCartridgeMode_aDawliah_64K:		return 70;
		case kATCartridgeMode_5200_64K_32KBanks:	return 71;
		case kATCartridgeMode_5200_128K_32KBanks:	return 72;
		case kATCartridgeMode_5200_256K_32KBanks:	return 73;
		case kATCartridgeMode_5200_512K_32KBanks:	return 74;
		case kATCartridgeMode_MaxFlash_1024K_Bank0:	return 75;
		case kATCartridgeMode_Williams_16K:			return 76;

		case kATCartridgeMode_TelelinkII:			return 78;
		case kATCartridgeMode_Pronto:				return 79;

		case kATCartridgeMode_MaxFlash_128K_MyIDE:		return g_ATCVCartType_MaxFlash128KMyIDE;
		//case kATCartridgeMode_MegaCart_1M_2:
		//case kATCartridgeMode_MegaCart_512K_3:
		case kATCartridgeMode_BountyBob5200Alt:			return g_ATCVCartType_BountyBob5200Alt;

		case kATCartridgeMode_JRC_RAMBOX:				return 80;
		case kATCartridgeMode_MDDOS:					return 81;
		case kATCartridgeMode_COS32K:					return 82;
		case kATCartridgeMode_SICPlus:					return 83;
		case kATCartridgeMode_Corina_1M_EEPROM:			return 84;
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:	return 85;
		case kATCartridgeMode_XEMulticart_8K:			return 86;
		case kATCartridgeMode_XEMulticart_16K:			return 87;
		case kATCartridgeMode_XEMulticart_32K:			return 88;
		case kATCartridgeMode_XEMulticart_64K:			return 89;
		case kATCartridgeMode_XEMulticart_128K:			return 90;
		case kATCartridgeMode_XEMulticart_256K:			return 91;
		case kATCartridgeMode_XEMulticart_512K:			return 92;
		case kATCartridgeMode_XEMulticart_1M:			return 93;

		case kATCartridgeMode_RamCart_64K: return 94;
		case kATCartridgeMode_RamCart_128K: return 95;
		case kATCartridgeMode_DoubleRamCart_256K: return 96;
		case kATCartridgeMode_RamCart_1M: return 97;
		case kATCartridgeMode_RamCart_2M: return 98;
		case kATCartridgeMode_RamCart_4M: return 99;
		case kATCartridgeMode_RamCart_8M: return 100;
		case kATCartridgeMode_RamCart_16M: return 101;
		case kATCartridgeMode_RamCart_32M: return 102;
		case kATCartridgeMode_SiDiCar_32K: return 103;
		case kATCartridgeMode_JAtariCart_8K:			return 104;
		case kATCartridgeMode_JAtariCart_16K:			return 105;
		case kATCartridgeMode_JAtariCart_32K:			return 106;
		case kATCartridgeMode_JAtariCart_64K:			return 107;
		case kATCartridgeMode_JAtariCart_128K:			return 108;
		case kATCartridgeMode_JAtariCart_256K:			return 109;
		case kATCartridgeMode_JAtariCart_512K:			return 110;
		case kATCartridgeMode_JAtariCart_1024K:			return 111;
		case kATCartridgeMode_DCart:					return 112;

		case kATCartridgeMode_JRC6_64K:					return 160;

		default:
			return 0;
	}
}

bool ATIsCartridge5200Mode(ATCartridgeMode cartmode) {
	bool cartIs5200 = false;

	switch(cartmode) {
		case kATCartridgeMode_5200_32K:
		case kATCartridgeMode_5200_16K_TwoChip:
		case kATCartridgeMode_5200_16K_OneChip:
		case kATCartridgeMode_5200_8K:
		case kATCartridgeMode_5200_4K:
		case kATCartridgeMode_5200_64K_32KBanks:
		case kATCartridgeMode_5200_128K_32KBanks:
		case kATCartridgeMode_5200_256K_32KBanks:
		case kATCartridgeMode_5200_512K_32KBanks:
		case kATCartridgeMode_BountyBob5200:
		case kATCartridgeMode_BountyBob5200Alt:
			cartIs5200 = true;
			break;
	}

	return cartIs5200;
}

uint32 ATGetImageSizeForCartridgeType(ATCartridgeMode mode) {
	switch(mode) {
		case kATCartridgeMode_8K:					return 0x2000;
		case kATCartridgeMode_16K:					return 0x4000;
		case kATCartridgeMode_XEGS_32K:				return 0x8000;
		case kATCartridgeMode_XEGS_64K:				return 0x10000;
		case kATCartridgeMode_XEGS_128K:			return 0x20000;
		case kATCartridgeMode_Switchable_XEGS_32K:	return 0x8000;
		case kATCartridgeMode_Switchable_XEGS_64K:	return 0x10000;
		case kATCartridgeMode_Switchable_XEGS_128K:	return 0x20000;
		case kATCartridgeMode_Switchable_XEGS_256K:	return 0x40000;
		case kATCartridgeMode_Switchable_XEGS_512K:	return 0x80000;
		case kATCartridgeMode_Switchable_XEGS_1M:	return 0x100000;
		case kATCartridgeMode_MaxFlash_128K:		return 0x20000;
		case kATCartridgeMode_MaxFlash_128K_MyIDE:	return 0x20000;
		case kATCartridgeMode_MaxFlash_1024K:		return 0x100000;
		case kATCartridgeMode_MegaCart_16K:			return 0x4000;
		case kATCartridgeMode_MegaCart_32K:			return 0x8000;
		case kATCartridgeMode_MegaCart_64K:			return 0x10000;
		case kATCartridgeMode_MegaCart_128K:		return 0x20000;
		case kATCartridgeMode_MegaCart_256K:		return 0x40000;
		case kATCartridgeMode_MegaCart_512K:		return 0x80000;
		case kATCartridgeMode_MegaCart_1M:			return 0x100000;
		case kATCartridgeMode_SuperCharger3D:		return 0;
		case kATCartridgeMode_BountyBob800:			return 0xA000;
		case kATCartridgeMode_OSS_034M:				return 0x4000;
		case kATCartridgeMode_OSS_M091:				return 0x4000;
		case kATCartridgeMode_5200_32K:				return 0x8000;
		case kATCartridgeMode_5200_16K_TwoChip:		return 0x4000;
		case kATCartridgeMode_5200_16K_OneChip:		return 0x4000;
		case kATCartridgeMode_5200_8K:				return 0x2000;
		case kATCartridgeMode_5200_4K:				return 0x1000;
		case kATCartridgeMode_Corina_1M_EEPROM:			return 1048576 + 8192;
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:	return 524288 + 8192;
		case kATCartridgeMode_BountyBob5200:		return 0xA000;
		case kATCartridgeMode_SpartaDosX_128K:		return 0x20000;
		case kATCartridgeMode_TelelinkII:			return 8192 + 256;
		case kATCartridgeMode_Williams_64K:			return 0x10000;
		case kATCartridgeMode_Diamond_64K:			return 0x10000;
		case kATCartridgeMode_Express_64K:			return 0x10000;
		case kATCartridgeMode_SpartaDosX_64K:		return 0x10000;
		case kATCartridgeMode_RightSlot_8K:			return 0x2000;
		case kATCartridgeMode_XEGS_256K:			return 0x40000;
		case kATCartridgeMode_XEGS_512K:			return 0x80000;
		case kATCartridgeMode_XEGS_1M:				return 0x100000;
		case kATCartridgeMode_DB_32K:				return 0x8000;
		case kATCartridgeMode_Atrax_128K:			return 0x20000;
		case kATCartridgeMode_Williams_32K:			return 0x8000;
		case kATCartridgeMode_Phoenix_8K:			return 0x2000;
		case kATCartridgeMode_Blizzard_16K:			return 0x4000;
		case kATCartridgeMode_Blizzard_32K:			return 0x8000;
		case kATCartridgeMode_SIC_128K:				return 0x20000;
		case kATCartridgeMode_SIC_256K:				return 0x40000;
		case kATCartridgeMode_SIC_512K:				return 0x80000;
		case kATCartridgeMode_Atrax_SDX_128K:		return 0x20000;
		case kATCartridgeMode_OSS_043M:				return 0x4000;
		case kATCartridgeMode_OSS_8K:				return 0x2000;
		case kATCartridgeMode_Blizzard_4K:			return 0x1000;
		case kATCartridgeMode_AST_32K:				return 0x8000;
		case kATCartridgeMode_Atrax_SDX_64K:		return 0x10000;
		case kATCartridgeMode_Turbosoft_64K:		return 0x10000;
		case kATCartridgeMode_Turbosoft_128K:		return 0x20000;
		case kATCartridgeMode_MaxFlash_1024K_Bank0:	return 0x100000;
		case kATCartridgeMode_MegaCart_1M_2:		return 0x100000;
		case kATCartridgeMode_5200_64K_32KBanks:	return 0x10000;
		case kATCartridgeMode_5200_128K_32KBanks:	return 0x20000;
		case kATCartridgeMode_5200_256K_32KBanks:	return 0x40000;
		case kATCartridgeMode_5200_512K_32KBanks:	return 0x80000;
		case kATCartridgeMode_MicroCalc:			return 0x8000;
		case kATCartridgeMode_2K:					return 0x0800;
		case kATCartridgeMode_4K:					return 0x1000;
		case kATCartridgeMode_RightSlot_4K:			return 0x1000;
		case kATCartridgeMode_MegaCart_512K_3:		return 0x80000;
		case kATCartridgeMode_MegaCart_2M:			return 0x200000;
		case kATCartridgeMode_MegaCart_4M_3:		return 0x400000;
		case kATCartridgeMode_TheCart_32M:			return 32*1024*1024;
		case kATCartridgeMode_TheCart_64M:			return 64*1024*1024;
		case kATCartridgeMode_TheCart_128M:			return 128*1024*1024;
		case kATCartridgeMode_MegaMax_2M:			return 0x200000;
		case kATCartridgeMode_BountyBob5200Alt:		return 0xA000;
		case kATCartridgeMode_XEGS_64K_Alt:			return 0x10000;
		case kATCartridgeMode_Atrax_128K_Raw:		return 0x20000;
		case kATCartridgeMode_aDawliah_32K:			return 0x8000;
		case kATCartridgeMode_aDawliah_64K:			return 0x10000;
		case kATCartridgeMode_JRC6_64K:		return 0x10000;
		case kATCartridgeMode_JRC_RAMBOX:	return 0x10000;
		case kATCartridgeMode_XEMulticart_8K:		return 0x2000;
		case kATCartridgeMode_XEMulticart_16K:		return 0x4000;
		case kATCartridgeMode_XEMulticart_32K:		return 0x8000;
		case kATCartridgeMode_XEMulticart_64K:		return 0x10000;
		case kATCartridgeMode_XEMulticart_128K:		return 0x20000;
		case kATCartridgeMode_XEMulticart_256K:		return 0x40000;
		case kATCartridgeMode_XEMulticart_512K:		return 0x80000;
		case kATCartridgeMode_XEMulticart_1M:		return 0x100000;
		case kATCartridgeMode_SICPlus:				return 0x100000;
		case kATCartridgeMode_Williams_16K:			return 0x4000;
		case kATCartridgeMode_MDDOS:				return 0x10000;
		case kATCartridgeMode_COS32K:				return 0x8000;
		case kATCartridgeMode_Pronto:				return 0x4000;
		case kATCartridgeMode_RamCart_64K: return 0x10000;
		case kATCartridgeMode_RamCart_128K: return 0x20000;
		case kATCartridgeMode_DoubleRamCart_256K: return 0x40000;
		case kATCartridgeMode_RamCart_1M: return 0x100000;
		case kATCartridgeMode_RamCart_2M: return 0x200000;
		case kATCartridgeMode_RamCart_4M: return 0x400000;
		case kATCartridgeMode_RamCart_8M: return 0x800000;
		case kATCartridgeMode_RamCart_16M: return 0x1000000;
		case kATCartridgeMode_RamCart_32M: return 0x2000000;
		case kATCartridgeMode_SiDiCar_32K: return 0x8000;
		case kATCartridgeMode_JAtariCart_8K:		return 0x2000;
		case kATCartridgeMode_JAtariCart_16K:		return 0x4000;
		case kATCartridgeMode_JAtariCart_32K:		return 0x8000;
		case kATCartridgeMode_JAtariCart_64K:		return 0x10000;
		case kATCartridgeMode_JAtariCart_128K:		return 0x20000;
		case kATCartridgeMode_JAtariCart_256K:		return 0x40000;
		case kATCartridgeMode_JAtariCart_512K:		return 0x80000;
		case kATCartridgeMode_JAtariCart_1024K:		return 0x100000;
		case kATCartridgeMode_DCart:				return 0x80000;

		default:
			return 0;
	}
}

bool ATIsCartridgeRAMMode(ATCartridgeMode mode) {
	return mode >= kATCartridgeMode_RamCart_64K
		&& mode <= kATCartridgeMode_SiDiCar_32K;
}
