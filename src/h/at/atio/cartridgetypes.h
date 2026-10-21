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

#ifndef f_AT_ATIO_CARTRIDGETYPES_H
#define f_AT_ATIO_CARTRIDGETYPES_H

#include <vd2/system/vdtypes.h>

enum ATCartridgeMode : uint32 {
	kATCartridgeMode_None,
	kATCartridgeMode_8K,
	kATCartridgeMode_16K,
	kATCartridgeMode_XEGS_32K,
	kATCartridgeMode_XEGS_64K,
	kATCartridgeMode_XEGS_128K,
	kATCartridgeMode_Switchable_XEGS_32K,
	kATCartridgeMode_Switchable_XEGS_64K,
	kATCartridgeMode_Switchable_XEGS_128K,
	kATCartridgeMode_Switchable_XEGS_256K,
	kATCartridgeMode_Switchable_XEGS_512K,
	kATCartridgeMode_Switchable_XEGS_1M,
	kATCartridgeMode_MaxFlash_128K,
	kATCartridgeMode_MaxFlash_128K_MyIDE,
	kATCartridgeMode_MaxFlash_1024K,
	kATCartridgeMode_MegaCart_16K,
	kATCartridgeMode_MegaCart_32K,
	kATCartridgeMode_MegaCart_64K,
	kATCartridgeMode_MegaCart_128K,
	kATCartridgeMode_MegaCart_256K,
	kATCartridgeMode_MegaCart_512K,
	kATCartridgeMode_MegaCart_1M,
	kATCartridgeMode_MegaCart_2M,
	kATCartridgeMode_SuperCharger3D,
	kATCartridgeMode_BountyBob800,
	kATCartridgeMode_OSS_034M,
	kATCartridgeMode_OSS_M091,
	kATCartridgeMode_5200_32K,
	kATCartridgeMode_5200_16K_TwoChip,
	kATCartridgeMode_5200_16K_OneChip,
	kATCartridgeMode_5200_8K,
	kATCartridgeMode_5200_4K,
	kATCartridgeMode_Corina_1M_EEPROM,
	kATCartridgeMode_Corina_512K_SRAM_EEPROM,
	kATCartridgeMode_BountyBob5200,
	kATCartridgeMode_SpartaDosX_128K,
	kATCartridgeMode_TelelinkII,
	kATCartridgeMode_Williams_64K,
	kATCartridgeMode_Diamond_64K,
	kATCartridgeMode_Express_64K,
	kATCartridgeMode_SpartaDosX_64K,
	kATCartridgeMode_RightSlot_8K,
	kATCartridgeMode_XEGS_256K,
	kATCartridgeMode_XEGS_512K,
	kATCartridgeMode_XEGS_1M,
	kATCartridgeMode_DB_32K,
	kATCartridgeMode_Atrax_128K,
	kATCartridgeMode_Williams_32K,
	kATCartridgeMode_Phoenix_8K,
	kATCartridgeMode_Blizzard_16K,
	kATCartridgeMode_Blizzard_32K,
	kATCartridgeMode_SIC_128K,
	kATCartridgeMode_SIC_256K,
	kATCartridgeMode_SIC_512K,
	kATCartridgeMode_Atrax_SDX_128K,
	kATCartridgeMode_OSS_043M,
	kATCartridgeMode_OSS_8K,
	kATCartridgeMode_Blizzard_4K,
	kATCartridgeMode_AST_32K,
	kATCartridgeMode_Atrax_SDX_64K,
	kATCartridgeMode_Turbosoft_64K,
	kATCartridgeMode_Turbosoft_128K,
	kATCartridgeMode_MaxFlash_1024K_Bank0,
	kATCartridgeMode_MegaCart_1M_2,			// Hardware by Bernd for ABBUC JHV 2009
	kATCartridgeMode_5200_64K_32KBanks,		// Used by M.U.L.E. 64K conversion
	kATCartridgeMode_5200_128K_32KBanks,
	kATCartridgeMode_5200_256K_32KBanks,
	kATCartridgeMode_5200_512K_32KBanks,
	kATCartridgeMode_MicroCalc,
	kATCartridgeMode_2K,
	kATCartridgeMode_4K,
	kATCartridgeMode_RightSlot_4K,
	kATCartridgeMode_MegaCart_512K_3,
	kATCartridgeMode_MegaCart_4M_3,
	kATCartridgeMode_TheCart_32M,
	kATCartridgeMode_TheCart_64M,
	kATCartridgeMode_TheCart_128M,
	kATCartridgeMode_MegaMax_2M,
	kATCartridgeMode_BountyBob5200Alt,		// Same as cart mapper 7 except that the fixed bank is first.
	kATCartridgeMode_XEGS_64K_Alt,			// (type 67)
	kATCartridgeMode_Atrax_128K_Raw,		// (type 68)
	kATCartridgeMode_aDawliah_32K,			// (type 69)
	kATCartridgeMode_aDawliah_64K,			// (type 70)
	kATCartridgeMode_JRC_RAMBOX,
	kATCartridgeMode_XEMulticart_8K,		// XE Multicart by Idorobots (Kajetan Rzepecki)
	kATCartridgeMode_XEMulticart_16K,
	kATCartridgeMode_XEMulticart_32K,
	kATCartridgeMode_XEMulticart_64K,
	kATCartridgeMode_XEMulticart_128K,
	kATCartridgeMode_XEMulticart_256K,
	kATCartridgeMode_XEMulticart_512K,
	kATCartridgeMode_XEMulticart_1M,
	kATCartridgeMode_SICPlus,
	kATCartridgeMode_Williams_16K,
	kATCartridgeMode_JRC6_64K,		// (unofficial type 160 used by AVGCart)
	kATCartridgeMode_MDDOS,
	kATCartridgeMode_COS32K,
	kATCartridgeMode_Pronto,
	kATCartridgeMode_JAtariCart_8K,			// J(atari)Cart by Jakub Husak (https://github.com/jhusak/jataricart)
	kATCartridgeMode_JAtariCart_16K,
	kATCartridgeMode_JAtariCart_32K,
	kATCartridgeMode_JAtariCart_64K,
	kATCartridgeMode_JAtariCart_128K,
	kATCartridgeMode_JAtariCart_256K,
	kATCartridgeMode_JAtariCart_512K,
	kATCartridgeMode_JAtariCart_1024K,
	kATCartridgeMode_DCart,					// DCart by Grzegorz "GienekP" Karpiel (https://dcart.pl)
	kATCartridgeMode_RamCart_64K,
	kATCartridgeMode_RamCart_128K,
	kATCartridgeMode_DoubleRamCart_256K,
	kATCartridgeMode_RamCart_1M,
	kATCartridgeMode_RamCart_2M,
	kATCartridgeMode_RamCart_4M,
	kATCartridgeMode_RamCart_8M,
	kATCartridgeMode_RamCart_16M,
	kATCartridgeMode_RamCart_32M,
	kATCartridgeMode_SiDiCar_32K,
	kATCartridgeModeCount
};

static constexpr uint32 kATCartridgeMapper_Max = 160;

ATCartridgeMode ATGetCartridgeModeForMapper(int mapper);
int ATGetCartridgeMapperForMode(ATCartridgeMode mode, uint32 size);
// Battery-backed RAM cartridge images are mutable and included in snapshots.
bool ATIsCartridgeRAMMode(ATCartridgeMode mode);
bool ATIsCartridge5200Mode(ATCartridgeMode cartmode);
uint32 ATGetImageSizeForCartridgeType(ATCartridgeMode mode);

#endif
