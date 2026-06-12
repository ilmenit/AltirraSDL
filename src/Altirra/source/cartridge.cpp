//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2009 Avery Lee
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
//	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/zip.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/address.h>
#include <at/atcore/checksum.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/vfs.h>
#include "cartridge.h"
#include "savestate.h"
#include "options.h"
#include "oshelper.h"
#include "resource.h"
#include "simulator.h"
#include "uirender.h"

bool ATIsCartridgeModeHWCompatible(ATCartridgeMode cartmode, int hwmode) {
	bool modeIs5200 = (hwmode == kATHardwareMode_5200);

	return ATIsCartridge5200Mode(cartmode) == modeIs5200;
}

///////////////////////////////////////////////////////////////////////////

struct ATCartridgeModeInfo {
	sint32 mInitialBank = -1;
	sint32 mInitialBank2 = -1;

	sint32 mBank1Min = -1;
	sint32 mBank1Max = -1;
	sint32 mBank2Min = -1;
	sint32 mBank2Max = -1;

	// fixed memory window #1
	uint8 mFixed1Base = 0;
	uint8 mFixed1Size = 0;
	uint8 mFixed1Mask = 0;
	uint32 mFixed1Offset = 0;

	// fixed memory window #2
	uint8 mFixed2Base = 0;
	uint8 mFixed2Size = 0;
	uint8 mFixed2Mask = 0;
	uint32 mFixed2Offset = 0;
	bool mFixed2Ram = false;

	// banked window #1
	uint8 mBank1Base = 0;
	uint8 mBank1Size = 0;
	uint8 mBank1Mask = 0;

	// banked window #2
	uint32 mBank2Base = 0;
	uint32 mBank2Size = 0;

	uint8 mSpec1Base = 0;
	uint8 mSpec1Size = 0;
	ATMemoryReadHandler mpSpec1DebugRead = nullptr;
	ATMemoryReadHandler mpSpec1Read = nullptr;
	ATMemoryWriteHandler mpSpec1Write = nullptr;

	bool mUseCctl = false;
	bool mUseCctlRead = false;
	bool mUseCctlWrite = false;
	ATMemoryReadHandler mpCctlDebugRead = nullptr;
	ATMemoryReadHandler mpCctlRead = nullptr;
	ATMemoryWriteHandler mpCctlWrite = nullptr;

	uint32 mRamSize = 0;
	uint8 mRamFillByte = 0;

	bool mSuppressBasicDisable = false;
};

struct ATCartridgeModeTable {
	ATCartridgeModeInfo table[kATCartridgeModeCount] {};

	constexpr ATCartridgeModeTable();
};

struct ATCartridgeModeBuilder {
	ATCartridgeModeInfo& m;

	constexpr auto& InitBank(sint32 bank, sint32 minBank, sint32 maxBank) {
		m.mInitialBank = bank;
		m.mBank1Min = minBank;
		m.mBank1Max = maxBank;
		return *this;
	}

	constexpr auto& InitBank2(sint32 bank2, sint32 minBank, sint32 maxBank) {
		m.mInitialBank2 = bank2;
		m.mBank2Min = minBank;
		m.mBank2Max = maxBank;
		return *this;
	}

	constexpr auto& MapFixed1(uint8 base, uint8 size, uint8 mask = 0xFF, uint32 offset = 0) {
		if (0x100 - base < size) {
			throw 0;
		}

		m.mFixed1Base = base;
		m.mFixed1Size = size;
		m.mFixed1Mask = mask;
		m.mFixed1Offset = offset;

		return *this;
	}

	constexpr auto& MapFixed2(uint8 base, uint8 size, uint8 mask = 0xFF, uint32 offset = 0) {
		if (0x100 - base < size) {
			throw 0;
		}

		m.mFixed2Base = base;
		m.mFixed2Size = size;
		m.mFixed2Mask = mask;
		m.mFixed2Offset = offset;

		return *this;
	}

	constexpr auto& CCTLWrite(ATMemoryWriteHandler write) {
		m.mUseCctl = true;
		m.mUseCctlWrite = true;
		m.mpCctlWrite = write;

		return *this;
	}
	
	constexpr auto& CCTLReadWrite(ATMemoryReadHandler debugRead, ATMemoryReadHandler read, ATMemoryWriteHandler write) {
		m.mUseCctl = true;
		m.mUseCctlRead = true;
		m.mUseCctlWrite = true;
		m.mpCctlDebugRead = debugRead;
		m.mpCctlRead = read;
		m.mpCctlWrite = write;

		return *this;
	}

	constexpr auto& MapBank1_CCTLWrite(uint8 base, uint8 size, ATMemoryWriteHandler write) {
		m.mBank1Base = base;
		m.mBank1Size = size;
		m.mBank1Mask = 0xFF;

		return CCTLWrite(write);
	}
	
	constexpr auto& MapBank1_CCTLReadWrite(uint8 base, uint8 size, ATMemoryReadHandler debugRead, ATMemoryReadHandler read, ATMemoryWriteHandler write) {
		m.mBank1Base = base;
		m.mBank1Size = size;
		m.mBank1Mask = 0xFF;

		return CCTLReadWrite(debugRead, read, write);
	}
	
	constexpr auto& MapBank2() {
		m.mBank2Base = 0x80;
		m.mBank2Size = 0x20;
		return *this;
	}

	constexpr auto& MapBank2(uint8 base, uint8 size) {
		m.mBank2Base = base;
		m.mBank2Size = size;
		return *this;
	}

	constexpr auto& MapSpec1(uint8 base, uint8 size, ATMemoryReadHandler debugRead, ATMemoryReadHandler read, ATMemoryWriteHandler write) {
		m.mSpec1Base = base;
		m.mSpec1Size = size;
		m.mpSpec1DebugRead = debugRead;
		m.mpSpec1Read = read;
		m.mpSpec1Write = write;
		return *this;
	}

	constexpr auto& HasRam(uint32 size, uint8 fillByte) {
		m.mRamSize = size;
		m.mRamFillByte = fillByte;
		return *this;
	}

	constexpr auto& NoBasicDisable() {
		m.mSuppressBasicDisable = true;
		return *this;
	}
};

struct ATCartridgeModeTableBuilder {
	ATCartridgeModeTable& m;

	constexpr ATCartridgeModeBuilder operator[](ATCartridgeMode mode) const {
		return ATCartridgeModeBuilder{m.table[mode]};
	}
};

constexpr ATCartridgeModeTable::ATCartridgeModeTable() {
	typedef ATCartridgeEmulator CE;

	ATCartridgeModeTableBuilder m{*this};

	m[kATCartridgeMode_5200_4K]
		// 4K ROM mapped to $4000-BFFF
		.InitBank(0, 0, 0)
		.MapFixed1(0x40, 0x80, 0x0F)
		;

	m[kATCartridgeMode_5200_8K]
		// 8K ROM mapped to $4000-BFFF
		.InitBank(0, 0, 0)
		.MapFixed1(0x40, 0x80, 0x1F)
		;

	m[kATCartridgeMode_5200_16K_TwoChip]
		// 16K ROM mapped to $4000-BFFF with A14 selecting 8K half
		.InitBank(0, 0, 0)
		.MapFixed1(0x40, 0x40, 0x1F, 0)
		.MapFixed2(0x80, 0x40, 0x1F, 0x2000)
		;

	m[kATCartridgeMode_5200_16K_OneChip]
		// 16K ROM mapped to $4000-BFFF with A13 selecting 8K half
		.InitBank(0, 0, 0)
		.MapFixed1(0x40, 0x80, 0x3F)
		;

	m[kATCartridgeMode_5200_32K]
		// 32K ROM mapped to $4000-BFFF
		.InitBank(0, 0, 0)
		.MapFixed1(0x40, 0x80)
		;

	m[kATCartridgeMode_2K]
		// 2K ROM mapped to $A000-BFFF; $A000-B7FF open
		.InitBank(0, 0, 0)
		.MapFixed1(0xA0, 0x20)
		;

	m[kATCartridgeMode_4K]
		// 4K ROM mapped to $B000-BFFF; $A000-AFFF open
		.InitBank(0, 0, 0)
		.MapFixed1(0xA0, 0x20)
		;

	m[kATCartridgeMode_8K]
		// 8K ROM mapped to $A000-BFFF
		.InitBank(0, 0, 0)
		.MapFixed1(0xA0, 0x20)
		;

	m[kATCartridgeMode_16K]
		// 16K ROM mapped to $8000-BFFF
		.InitBank(0, 0, 0)
		.MapFixed1(0x80, 0x40)
		;

	m[kATCartridgeMode_RightSlot_4K]
		// 4K ROM mapped to $8000-9FFF (expanded to 8K on load)
		.InitBank(0, 0, 0)
		.MapFixed1(0x80, 0x20)
		;

	m[kATCartridgeMode_RightSlot_8K]
		// 8K ROM mapped to $8000-9FFF
		.InitBank(0, 0, 0)
		.MapFixed1(0x80, 0x20)
		;

	m[kATCartridgeMode_TelelinkII]
		.InitBank(0, 0, 0)
		.HasRam(256, 0xFF)
		;

	m[kATCartridgeMode_XEGS_32K]
		// 4 x 8K banks mapped to $8000-9FFF by D0-D1 written to CCTL
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, 0, 3)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x6000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank<0x03>)
		;

	m[kATCartridgeMode_XEGS_64K]
		// 8 x 8K banks mapped to $8000-9FFF by D0-D2 written to CCTL
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, 0, 7)
		.MapFixed1(0xA0, 0x20, 0xFF, 0xE000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank<0x07>)
		;

	m[kATCartridgeMode_XEGS_64K_Alt]
		// Similar to XEGS 64K except with 4-bit banking instead of
		// 3-bit (essentially, 64K of ROM in a 128K cart).
		// Reference: Atari800 4.0.0 CART.txt, type 67
		.InitBank(0, 0, 7)
		.MapFixed1(0xA0, 0x20, 0xFF, 0xE000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_XEGS_64K_Alt)
		.MapSpec1(0x80, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_Unmapped, CE::WriteByte_Unmapped)
		;

	m[kATCartridgeMode_XEGS_128K]
		// 16 x 8K banks mapped to $8000-9FFF by D0-D3 written to CCTL
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, 0, 15)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x1E000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank<0x0F>)
		;

	m[kATCartridgeMode_XEGS_256K]
		// 32 x 8K banks mapped to $8000-9FFF by D0-D4 written to CCTL
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, 0, 31)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x3E000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank<0x1F>)
		;

	m[kATCartridgeMode_XEGS_512K]
		// 64 x 8K banks mapped to $8000-9FFF by D0-D5 written to CCTL
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, 0, 63)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x7E000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank<0x3F>)
		;

	m[kATCartridgeMode_XEGS_1M]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, 0, 127)
		.MapFixed1(0xA0, 0x20, 0xFF, 0xFE000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank<0x7F>)
		;

	m[kATCartridgeMode_Switchable_XEGS_32K]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL, D7=1 disables cartridge (including fixed bank)
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, -1, 3)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x6000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank_Switchable<0x03>)
		;

	m[kATCartridgeMode_Switchable_XEGS_64K]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL, D7=1 disables cartridge (including fixed bank)
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, -1, 7)
		.MapFixed1(0xA0, 0x20, 0xFF, 0xE000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank_Switchable<0x07>)
		;

	m[kATCartridgeMode_Switchable_XEGS_128K]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL, D7=1 disables cartridge (including fixed bank)
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, -1, 15)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x1E000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank_Switchable<0x0F>)
		;

	m[kATCartridgeMode_Switchable_XEGS_256K]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL, D7=1 disables cartridge (including fixed bank)
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, -1, 31)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x3E000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank_Switchable<0x1F>)
		;

	m[kATCartridgeMode_Switchable_XEGS_512K]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL, D7=1 disables cartridge (including fixed bank)
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, -1, 63)
		.MapFixed1(0xA0, 0x20, 0xFF, 0x7E000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank_Switchable<0x3F>)
		;

	m[kATCartridgeMode_Switchable_XEGS_1M]
		// 128 x 8K banks mapped to $8000-9FFF by D0-D6 written to CCTL, D7=1 disables cartridge (including fixed bank)
		// Last 8K bank mapped to $A000-BFFF
		.InitBank(0, -1, 127)
		.MapFixed1(0xA0, 0x20, 0xFF, 0xFE000)
		.MapBank1_CCTLWrite(0x80, 0x20, CE::WriteByte_CCTL_DataToBank_Switchable<0x7F>)
		;

	m[kATCartridgeMode_MaxFlash_128K].InitBank(0, -1, 15);
	m[kATCartridgeMode_MaxFlash_128K_MyIDE].InitBank(0, -1, 15);

	m[kATCartridgeMode_MegaCart_16K]
		.InitBank(0, -1, 0)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x00>)
		;
	m[kATCartridgeMode_MegaCart_32K]
		.InitBank(0, -1, 1)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x01>)
		;
	m[kATCartridgeMode_MegaCart_64K]
		.InitBank(0, -1, 3)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x03>)
		;
	m[kATCartridgeMode_MegaCart_128K]
		.InitBank(0, -1, 7)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x07>)
		;
	m[kATCartridgeMode_MegaCart_256K]
		.InitBank(0, -1, 15)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x0F>)
		;
	m[kATCartridgeMode_MegaCart_512K]
		.InitBank(0, -1, 31)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x1F>)
		;
	m[kATCartridgeMode_MegaCart_1M]
		.InitBank(0, -1, 63)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x3F>)
		;

	m[kATCartridgeMode_MegaCart_2M]
		.InitBank(0, -1, 127)
		.MapBank1_CCTLWrite(0x80, 0x40, CE::WriteByte_CCTL_DataToBank_Switchable<0x7F>)
		;
	
	m[kATCartridgeMode_MaxFlash_1024K].InitBank(127, -1, 127);
	m[kATCartridgeMode_MaxFlash_1024K_Bank0].InitBank(0, -1, 127);
	m[kATCartridgeMode_BountyBob800]
		.InitBank(0, 0, 3)
		.InitBank2(0, 0, 3)
		;

	m[kATCartridgeMode_BountyBob5200]
		.InitBank(0, 0, 3)
		.InitBank2(0, 0, 3)
		;

	m[kATCartridgeMode_BountyBob5200Alt]
		.InitBank(0, 0, 3)
		.InitBank2(0, 0, 3)
		;

	m[kATCartridgeMode_OSS_034M]
		.InitBank(2, -1, 6)
		.InitBank2(0, -1, 0)
		;

	m[kATCartridgeMode_OSS_043M]
		.InitBank(2, -1, 6)
		.InitBank2(0, -1, 0)
		;

	m[kATCartridgeMode_OSS_M091]
		.InitBank(3, -1, 6)
		.InitBank2(0, -1, 0);

	m[kATCartridgeMode_OSS_8K]
		.InitBank(1, -1, 1)
		.InitBank2(0, -1, 0);

	m[kATCartridgeMode_Corina_1M_EEPROM]
		.InitBank(0, -1, 64)
		;

	m[kATCartridgeMode_Corina_512K_SRAM_EEPROM]
		.InitBank(0, -1, 64)
		.HasRam(524288, 0)
		;

	m[kATCartridgeMode_SpartaDosX_64K]
		.InitBank(0, -1, 8)
		;

	m[kATCartridgeMode_Atrax_SDX_64K]
		.InitBank(0, -1, 7)
		;

	m[kATCartridgeMode_SpartaDosX_128K]
		.InitBank(0, -1, 16)
		;
	
	m[kATCartridgeMode_Atrax_SDX_128K]
		.InitBank(0, -1, 15)
		;

	m[kATCartridgeMode_Williams_64K]
		.InitBank(0, -1, 7)
		;

	m[kATCartridgeMode_Williams_32K]
		.InitBank(0, -1, 3)
		;

	m[kATCartridgeMode_Williams_16K]
		.InitBank(0, -1, 1)
		;

	m[kATCartridgeMode_Diamond_64K]
		.InitBank(0, 0, 7)
		;

	m[kATCartridgeMode_Express_64K]
		.InitBank(0, 0, 7)
		;

	m[kATCartridgeMode_DB_32K]
		// Fixed bank 3 mapped to $A000-BFFF.
		// Variable banks 0-3 mapped to $8000-9FFF, switched by address of write to CCTL.
		.InitBank(0, 0, 3)
		;

	m[kATCartridgeMode_Atrax_128K]
		.InitBank(0, -1, 15)
		;

	m[kATCartridgeMode_Atrax_128K_Raw]
		.InitBank(0, -1, 15)
		;

	m[kATCartridgeMode_Phoenix_8K]
		.InitBank(0, -1, 0)
		;

	m[kATCartridgeMode_Blizzard_4K]
		.InitBank(0, -1, 0)
		;

	m[kATCartridgeMode_Blizzard_16K]
		.InitBank(0, -1, 0)
		;

	m[kATCartridgeMode_Blizzard_32K]
		.InitBank(0, -1, 3)
		;

	m[kATCartridgeMode_SIC_128K]
		.InitBank(0, 0, 255)
		;

	m[kATCartridgeMode_SIC_256K]
		.InitBank(0, 0, 255)
		;

	m[kATCartridgeMode_SIC_512K]
		.InitBank(0, 0, 255)
		;

	m[kATCartridgeMode_AST_32K]
		// $A000-BFFF initially mapped and becomes unmapped on CCTL write.
		// $D500-D5FF mapps successive pages on CCTL write.
		.InitBank(0, -1, 0)
		.InitBank2(0, 0, 127)
		;

	m[kATCartridgeMode_Turbosoft_64K]
		.InitBank(0, -1, 7)
		;

	m[kATCartridgeMode_Turbosoft_128K]
		.InitBank(0, -1, 15)
		;

	m[kATCartridgeMode_MegaCart_1M_2]
		// 64 banks of 8K mapped to $A000-BFFF.
		// Write to CCTL switches bank by D0-D5, D7=1 disables.
		.InitBank(0, -1, 63)
		;

	m[kATCartridgeMode_MegaCart_512K_3]
		// 32 banks of 16K mapped to $8000-BFFF switched by write to CCTL.
		// Writing $FF disables. Read from CCTL returns current bank.
		.InitBank(0, -1, 254)
		;

	m[kATCartridgeMode_MegaCart_4M_3]
		// 255 banks of 16K mapped to $8000-BFFF switched by write to CCTL.
		// Writing $FF disables. Read from CCTL returns current bank.
		.InitBank(0xFE, -1, 254)
		;

	m[kATCartridgeMode_5200_64K_32KBanks]
		// Access to $BFE0-BFFF resets to home bank 1.
		// Access to $BFD0-BFDF selects bank according to A2.
		.InitBank(0, 0, 1)
		;

	m[kATCartridgeMode_5200_128K_32KBanks]
		// Access to $BFE0-BFFF resets to home bank 1.
		// Access to $BFD0-BFDF changes lower two bank bits by A2-A3.
		.InitBank(0, 0, 3)
		;

	m[kATCartridgeMode_5200_256K_32KBanks]
		// Access to $BFE0-BFFF resets to home bank 1.
		// Access to $BFD0-BFDF changes lower two bank bits by A2-A3.
		// Access to $BFC0-BFCF changes upper bank bit by A2.
		.InitBank(0, 0, 7)
		;

	m[kATCartridgeMode_5200_512K_32KBanks]
		// Access to $BFE0-BFFF resets to home bank 1.
		// Access to $BFD0-BFDF changes lower two bank bits by A2-A3.
		// Access to $BFC0-BFCF changes upper two bank bits by A2-A3.
		.InitBank(0, 0, 15)
		;

	m[kATCartridgeMode_MicroCalc]
		.InitBank(0, -1, 3)
		;

	m[kATCartridgeMode_TheCart_32M]
		.InitBank(0, -1, 0x3FFFF)
		.InitBank2(-1, -1, 0x3FFFF)
		.HasRam(524288, 0)
		;

	m[kATCartridgeMode_TheCart_64M]
		.InitBank(0, -1, 0x3FFFF)
		.InitBank2(-1, -1, 0x3FFFF)
		.HasRam(524288, 0)
		;

	m[kATCartridgeMode_TheCart_128M]
		.InitBank(0, -1, 0x3FFFF)
		.InitBank2(-1, -1, 0x3FFFF)
		.HasRam(524288, 0)
		;

	m[kATCartridgeMode_MegaMax_2M]
		.InitBank(0, -1, 127)
		;

	m[kATCartridgeMode_aDawliah_32K]
		.InitBank(0, 0, 3)
		;

	m[kATCartridgeMode_aDawliah_64K]
		.InitBank(0, 0, 7)
		;

	// Power-up is not entirely guaranteed but should often be similar to reset
	// button, which clears the latch holding the bank.
	m[kATCartridgeMode_JRC_RAMBOX]
		.InitBank(0, -1, 7)			// 8 ROM banks x 8KB = 64KB ROM
		.InitBank2(0, 0, 2047)		// 2048 RAM banks x 128 bytes = 256KB RAM
		.HasRam(262144, 0)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_CCTL_JRC_RAMBOX, CE::ReadByte_CCTL_JRC_RAMBOX, CE::WriteByte_CCTL_JRC_RAMBOX)
		;

	// Power-up is not entirely guaranteed but should often be similar to reset
	// button, which clears the latch holding the bank -- but the ROM is addressed
	// by the inverted outputs, so this switches to last bank.
	m[kATCartridgeMode_JRC6_64K]
		.InitBank(7, -1, 7)
		.MapBank1_CCTLWrite(0xA0, 0x20, CE::WriteByte_CCTL_JRC6_64K)
		;

	m[kATCartridgeMode_XEMulticart_8K]
		.InitBank(0, 0, 0)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_16K]
		.InitBank(0, 0, 1)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_32K]
		.InitBank(0, 0, 3)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_64K]
		.InitBank(0, 0, 7)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_128K]
		.InitBank(0, 0, 15)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_256K]
		.InitBank(0, 0, 31)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_512K]
		.InitBank(0, 0, 63)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_XEMulticart_1M]
		.InitBank(0, 0, 127)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_XEMulticart, CE::WriteByte_CCTL_DataToBank<255>)
		.MapBank2()
		;

	m[kATCartridgeMode_SICPlus]
		.InitBank(0, 0, 255)
		;

	m[kATCartridgeMode_MDDOS]
		.InitBank(13, -1, 15)
		.InitBank2(12, -1, 15)
		.MapBank1_CCTLReadWrite(0xA0, 0x10, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_MDDOS, CE::WriteByte_CCTL_MDDOS)
		.MapBank2(0xB0, 0x10)
		;

	m[kATCartridgeMode_COS32K]
		.InitBank(1, 0, 1)
		.MapBank1_CCTLReadWrite(0x80, 0x40, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_COS32K, CE::WriteByte_CCTL_COS32K)
		;

	m[kATCartridgeMode_Pronto]
		.InitBank(0, 0, 0)
		.MapFixed1(0x80, 0x40)
		.MapSpec1(0x8F, 0x01, CE::ReadByte_Pronto, CE::ReadByte_Pronto, CE::WriteByte_Pronto)
		.CCTLReadWrite(CE::ReadByte_Unmapped, CE::ReadByte_CCTL_Pronto, CE::WriteByte_CCTL_Pronto)
		.HasRam(256, 0xFF)
		;

	m[kATCartridgeMode_JAtariCart_8K   ].InitBank(0, -1,   0).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x00>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x00>);
	m[kATCartridgeMode_JAtariCart_16K  ].InitBank(0, -1,   1).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x01>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x01>);
	m[kATCartridgeMode_JAtariCart_32K  ].InitBank(0, -1,   3).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x03>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x03>);
	m[kATCartridgeMode_JAtariCart_64K  ].InitBank(0, -1,   7).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x07>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x07>);
	m[kATCartridgeMode_JAtariCart_128K ].InitBank(0, -1,  15).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x0F>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x0F>);
	m[kATCartridgeMode_JAtariCart_256K ].InitBank(0, -1,  31).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x1F>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x1F>);
	m[kATCartridgeMode_JAtariCart_1024K].InitBank(0, -1, 127).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x7F>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x7F>);

	// 512K is special-cased as it has 1 flash chip + 1 empty socket
	m[kATCartridgeMode_JAtariCart_512K ].InitBank(0, -1,  63).MapBank1_CCTLReadWrite(0xA0, 0x20, CE::ReadByte_Unmapped, CE::ReadByte_CCTL_AddressToBank_Switchable<0x3F>, CE::WriteByte_CCTL_AddressToBank_Switchable<0x7F>);

	m[kATCartridgeMode_DCart]
		.InitBank(0, 0, 255)
		.MapBank1_CCTLReadWrite(0xA0, 0x20, CE::DebugReadByte_CCTL_DCart, CE::ReadByte_CCTL_DCart, CE::WriteByte_CCTL_AddressToBank<0xBF>)
		;

	// Suppress automatic internal BASIC disable for the MaxFlash cartridges since their
	// default loaders launch early in OS init and bypass cart boot on OPTION.
	m[kATCartridgeMode_MaxFlash_128K].NoBasicDisable();
	m[kATCartridgeMode_MaxFlash_1024K].NoBasicDisable();
	m[kATCartridgeMode_MaxFlash_1024K_Bank0].NoBasicDisable();
}

constexpr ATCartridgeModeTable kATCartridgeModeTable {};

///////////////////////////////////////////////////////////////////////////

ATCartridgeEmulator::ATCartridgeEmulator(int basePri, ATCartridgePriority cartPri, bool fastBus) {
	mBasePriority = basePri;
	mCartPriority = cartPri;
	mbFastBus = fastBus;

	ResetDebugBankMap();
}

ATCartridgeEmulator::~ATCartridgeEmulator() {
	Shutdown();
}

void ATCartridgeEmulator::GetDeviceInfo(ATDeviceInfo& info) {
	static constexpr ATDeviceDefinition sDef {
		.mpTag = "cartridge",
		.mpName = L"Cartridge",
		.mFlags = kATDeviceDefFlag_Internal | kATDeviceDefFlag_Hidden
	};

	info.mpDef = &sDef;
}

void ATCartridgeEmulator::Init() {
	mpMemMan = GetService<ATMemoryManager>();
	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
	mpUIRenderer = GetService<IATDeviceIndicatorManager>();

	mpCartridgePort->AddCartridge(this, mCartPriority, mCartId);
}

void ATCartridgeEmulator::Shutdown() {
	if (mpCartridgePort) {
		mpCartridgePort->RemoveCartridge(mCartId, this);
		mpCartridgePort = nullptr;
	}

	ShutdownMemoryLayers();
	mpScheduler = NULL;
	mpMemMan = NULL;
}

void ATCartridgeEmulator::SetFastBus(bool fastBus) {
	if (mbFastBus == fastBus)
		return;

	mbFastBus = fastBus;

	UpdateLayerBuses();
}

bool ATCartridgeEmulator::IsBASICDisableAllowed() const {
	return !kATCartridgeModeTable.table[mCartMode].mSuppressBasicDisable;
}

const wchar_t *ATCartridgeEmulator::GetPath() const {
	return mpImage ? mpImage->GetPath() : nullptr;
}

std::optional<uint32> ATCartridgeEmulator::GetImageFileCRC() const {
	if (!mpImage)
		return {};
	
	return mpImage->GetImageFileCRC();
}

std::optional<ATChecksumSHA256> ATCartridgeEmulator::GetImageFileSHA256() const {
	if (!mpImage)
		return {};
	
	return mpImage->GetImageFileSHA256();
}

void ATCartridgeEmulator::Load5200Default() {
	LoadNewCartridge(kATCartridgeMode_5200_4K);
	ATLoadKernelResource(IDR_NOCARTRIDGE, mpROM, 0, 4096, false);
	mbIsDefaultCart = true;
}

void ATCartridgeEmulator::LoadNewCartridge(ATCartridgeMode mode) {
	Unload();

	ATCreateCartridgeImage(mode, ~mpImage);

	InitFromImage();
}

bool ATCartridgeEmulator::Load(const wchar_t *s, ATCartLoadContext *loadCtx) {
	vdrefptr<ATVFSFileView> view;

	ATVFSOpenFileView(s, false, ~view);

	return Load(s, view->GetStream(), loadCtx);
}

bool ATCartridgeEmulator::Load(const wchar_t *origPath, IVDRandomAccessStream& f, ATCartLoadContext *loadCtx) {
	if (!ATLoadCartridgeImage(origPath, f, loadCtx, ~mpImage))
		return false;

	InitFromImage();
	return true;
}

void ATCartridgeEmulator::Load(IATCartridgeImage *image) {
	mpImage = image;
	InitFromImage();
}

void ATCartridgeEmulator::InitFromImage() {	
	mCartMode = mpImage->GetMode();

	// set initial bank and alloc size

	const auto& modeInfo = kATCartridgeModeTable.table[mCartMode];

	mInitialCartBank = modeInfo.mInitialBank;
	mInitialCartBank2 = modeInfo.mInitialBank2;

	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;

	mCartSize = mpImage->GetImageSize();
	mpROM = (uint8 *)mpImage->GetBuffer();

	mbIsDefaultCart = false;

	// initialize RAM
	mCARTRAM.clear();
	mCARTRAM.resize(modeInfo.mRamSize, modeInfo.mRamFillByte);

	InitDebugBankMap();
	InitMemoryLayers();
	ColdReset();

	mbDirty = mpImage->IsDirty();
}

void ATCartridgeEmulator::Unload() {
	ShutdownMemoryLayers();

	mpImage = nullptr;
	mpROM = nullptr;
	mCartSize = 0;
	mbIsDefaultCart = false;

	ResetDebugBankMap();
	
	vdfastvector<uint8>().swap(mCARTRAM);

	mInitialCartBank = 0;
	mInitialCartBank2 = 0;
	mCartBank = 0;
	mCartBank2 = 0;
	mCartMode = kATCartridgeMode_None;
}

void ATCartridgeEmulator::Save(const wchar_t *fn, bool includeHeader) {
	if (!mpImage)
		throw MyError("There is no cartridge to save.");

	ATSaveCartridgeImage(mpImage, fn, includeHeader);
	mbDirty = false;
}

void ATCartridgeEmulator::ColdReset() {
	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;
	UpdateCartBank();

	if (mpMemLayerVarBank2)
		UpdateCartBank2();

	switch(mCartMode) {
		case kATCartridgeMode_SuperCharger3D:
			memset(mSC3D, 0xFF, sizeof mSC3D);
			break;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			static const uint8 kTheCartInitData[]={
				0x00,
				0x00,
				0x01,
				0x00,
				0x00,
				0x00,
				0x01,
				0x00,
				0x82,	// /CS high, SI high, SCK low
			};

			memcpy(mTheCartRegs, kTheCartInitData, sizeof mTheCartRegs);
			mbTheCartConfigLock = false;
			mTheCartSICEnables = 0x40;		// Axxx only
			mTheCartOSSBank = 0;

			UpdateTheCartBanking();
			UpdateTheCart();
			break;
	}

	mFlashEmu.ColdReset();
	mFlashEmu2.ColdReset();
	mEEPROM.ColdReset();
}

sint32 ATCartridgeEmulator::ReadByte_Unmapped(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	return (uint8)thisptr->mpMemMan->ReadFloatingDataBus();
}

bool ATCartridgeEmulator::WriteByte_Unmapped(void *thisptr0, uint32 address, uint8 value) {
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_BB5200_1(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x4FF6;

	if (index < 4) {
		uint8 data = thisptr->mpROM[(thisptr->mCartBank << 12) + 0x0FF6 + index];
		thisptr->SetCartBank(index);
		return data;
	}

	return -1;
}

sint32 ATCartridgeEmulator::ReadByte_BB5200_2(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x5FF6;

	if (index < 4) {
		uint8 data = thisptr->mpROM[(thisptr->mCartBank2 << 12) + 0x4FF6 + index];
		thisptr->SetCartBank2(index);
		return data;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_BB5200_1(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x4FF6;

	if (index < 4)
		thisptr->SetCartBank(index);

	return true;
}

bool ATCartridgeEmulator::WriteByte_BB5200_2(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x5FF6;

	if (index < 4)
		thisptr->SetCartBank2(index);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_BB800_1(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x8FF6;

	if (index < 4) {
		uint8 data = thisptr->mpROM[(thisptr->mCartBank << 12) + 0x0FF6 + index];
		thisptr->SetCartBank(index);
		return data;
	}

	return -1;
}

sint32 ATCartridgeEmulator::ReadByte_BB800_2(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x9FF6;

	if (index < 4) {
		uint8 data = thisptr->mpROM[(thisptr->mCartBank2 << 12) + 0x4FF6 + index];
		thisptr->SetCartBank2(index);
		return data;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_BB800_1(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 index = address - 0x8FF6;

	if (index < 4)
		thisptr->SetCartBank(index);

	return true;
}

bool ATCartridgeEmulator::WriteByte_BB800_2(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 index = address - 0x9FF6;

	if (index < 4)
		thisptr->SetCartBank2(index);

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_SIC(void *thisptr0, uint32 address) {
	const ATCartridgeEmulator *thisptr = (const ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = ((thisptr->mCartBank * 0x4000) & thisptr->mCartSizeMask) + (address & 0x3fff);

	uint8 value;
	thisptr->mFlashEmu.DebugReadByte(fullAddr, value);

	return value;
}

sint32 ATCartridgeEmulator::ReadByte_SIC(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = ((thisptr->mCartBank * 0x4000) & thisptr->mCartSizeMask) + (address & 0x3fff);

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_SIC(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = ((thisptr->mCartBank * 0x4000) & thisptr->mCartSizeMask) + (address & 0x3fff);

	if (thisptr->mFlashEmu.WriteByte(fullAddr, value)) {
		if (thisptr->mFlashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty()) {
				thisptr->mbDirty = true;
				thisptr->mpImage->SetDirty();
			}
		}

		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();
		const bool flashReadBank1 = flashRead && !(thisptr->mCartBank & 0x40);
		const bool flashReadBank2 = flashRead && (thisptr->mCartBank & 0x20);

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashReadBank1);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashReadBank1);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashReadBank2);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashReadBank2);
	}

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_SICPlus(void *thisptr0, uint32 address) {
	const ATCartridgeEmulator *thisptr = (const ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = (thisptr->mCartBank & 0x1f) * 0x4000 + (address & 0x3fff);
	const auto& flashChip = thisptr->mCartBank & 0x80 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu;

	uint8 value;
	flashChip.DebugReadByte(fullAddr, value);

	return value;
}

sint32 ATCartridgeEmulator::ReadByte_SICPlus(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = (thisptr->mCartBank & 0x1f) * 0x4000 + (address & 0x3fff);
	auto& flashChip = thisptr->mCartBank & 0x80 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu;

	uint8 value;
	if (flashChip.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_SICPlus(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = (thisptr->mCartBank & 0x1f) * 0x4000 + (address & 0x3fff);
	auto& flashChip = thisptr->mCartBank & 0x80 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu;

	if (flashChip.WriteByte(fullAddr, value)) {
		if (flashChip.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && flashChip.IsDirty()) {
				thisptr->mbDirty = true;
				thisptr->mpImage->SetDirty();
			}
		}

		const bool flashRead = flashChip.IsControlReadEnabled();
		const bool flashReadBank1 = flashRead && !(thisptr->mCartBank & 0x40);
		const bool flashReadBank2 = flashRead && (thisptr->mCartBank & 0x20);

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashReadBank1);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashReadBank1);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashReadBank2);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashReadBank2);
	}

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_TheCart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 fullAddr = thisptr->mCartBank * 0x2000 + (address & 0x1fff);
	fullAddr &= thisptr->mCartSizeMask;

	uint8 value;
	if (thisptr->mFlashEmu.DebugReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

sint32 ATCartridgeEmulator::ReadByte_TheCart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 fullAddr = thisptr->mCartBank * 0x2000 + (address & 0x1fff);
	fullAddr &= thisptr->mCartSizeMask;

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_TheCart(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 fullAddr = (address & 0x2000 ? thisptr->mCartBank : thisptr->mCartBank2) * 0x2000 + (address & 0x1fff);
	fullAddr &= thisptr->mCartSizeMask;

	if (thisptr->mFlashEmu.WriteByte(fullAddr, value)) {
		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
	}

	if (thisptr->mFlashEmu.CheckForWriteActivity()) {
		thisptr->mpUIRenderer->SetFlashWriteActivity();

		if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty()) {
			thisptr->mbDirty = true;
			thisptr->mpImage->SetDirty();
		}
	}

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_MegaCart3(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	const uint32 fullAddr = (thisptr->mCartBank*0x4000 + (address & 0x3fff)) & ((uint32)thisptr->mCartSize - 1);

	uint8 value;
	if (thisptr->mFlashEmu.DebugReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

sint32 ATCartridgeEmulator::ReadByte_MegaCart3(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	const uint32 fullAddr = (thisptr->mCartBank*0x4000 + (address & 0x3fff)) & ((uint32)thisptr->mCartSize - 1);

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_MegaCart3(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return false;

	const uint32 fullAddr = (thisptr->mCartBank*0x4000 + (address & 0x3fff)) & ((uint32)thisptr->mCartSize - 1);

	if (thisptr->mFlashEmu.WriteByte(fullAddr, value)) {
		if (thisptr->mFlashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty()) {
				thisptr->mbDirty = true;
				thisptr->mpImage->SetDirty();
			}
		}

		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
	}

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_MaxFlash(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	const uint32 fullAddr = (thisptr->mCartBank*0x2000 + (address & 0x1fff)) & ((uint32)thisptr->mCartSize - 1);
	ATFlashEmulator& flashEmu = (fullAddr >= 0x80000 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu);

	uint8 value;
	if (flashEmu.DebugReadByte(fullAddr & 0x7FFFF, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

sint32 ATCartridgeEmulator::ReadByte_MaxFlash(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	const uint32 fullAddr = (thisptr->mCartBank*0x2000 + (address & 0x1fff)) & ((uint32)thisptr->mCartSize - 1);
	ATFlashEmulator& flashEmu = (fullAddr >= 0x80000 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu);

	uint8 value;
	if (flashEmu.ReadByte(fullAddr & 0x7FFFF, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_MaxFlash(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return false;

	const uint32 fullAddr = (thisptr->mCartBank*0x2000 + (address & 0x1fff)) & ((uint32)thisptr->mCartSize - 1);
	ATFlashEmulator& flashEmu = (fullAddr >= 0x80000 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu);

	if (flashEmu.WriteByte(fullAddr & 0x7FFFF, value)) {
		if (flashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && flashEmu.IsDirty()) {
				thisptr->mbDirty = true;
				thisptr->mpImage->SetDirty();
			}
		}

		const bool flashRead = flashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
	}

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_JAtariCart_512K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	if (thisptr->mCartBank >= 64)
		return thisptr->mpMemMan->ReadFloatingDataBus();

	const uint32 fullAddr = (thisptr->mCartBank*0x2000 + (address & 0x1fff)) & ((uint32)thisptr->mCartSize - 1);
	ATFlashEmulator& flashEmu = (fullAddr >= 0x40000 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu);

	uint8 value;
	if (flashEmu.DebugReadByte(fullAddr & 0x3FFFF, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

sint32 ATCartridgeEmulator::ReadByte_JAtariCart_512K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	if (thisptr->mCartBank >= 64)
		return thisptr->mpMemMan->ReadFloatingDataBus();

	const uint32 fullAddr = thisptr->mCartBank*0x2000 + (address & 0x1fff);

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_JAtariCart_512K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return false;

	if (thisptr->mCartBank >= 64)
		return true;

	const uint32 fullAddr = thisptr->mCartBank*0x2000 + (address & 0x1fff);

	if (thisptr->mFlashEmu.WriteByte(fullAddr & 0x3FFFF, value)) {
		if (thisptr->mFlashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty()) {
				thisptr->mbDirty = true;
				thisptr->mpImage->SetDirty();
			}
		}

		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
	}

	return true;
}

bool ATCartridgeEmulator::WriteByte_Corina1M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	// We don't emulate write times at the moment.
	if (thisptr->mCartBank == 64) {
		thisptr->mbDirty = true;
		thisptr->mpImage->SetDirty();

		if (thisptr->mpUIRenderer)
			thisptr->mpUIRenderer->SetFlashWriteActivity();

		thisptr->mpROM[0x100000 + (address & 0x1fff)] = value;
	}

	return true;
}

bool ATCartridgeEmulator::WriteByte_Corina512K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	// We don't emulate write times at the moment.
	if (thisptr->mCartBank == 64) {
		thisptr->mbDirty = true;
		thisptr->mpImage->SetDirty();

		if (thisptr->mpUIRenderer)
			thisptr->mpUIRenderer->SetFlashWriteActivity();

		thisptr->mpROM[0x80000 + (address & 0x1fff)] = value;
	}

	return true;
}

bool ATCartridgeEmulator::WriteByte_TelelinkII(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->mCARTRAM[address & 0xFF] = value | 0xF0;

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_Pronto(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	return thisptr->mCARTRAM[address - 0x8F00];
}

bool ATCartridgeEmulator::WriteByte_Pronto(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->mCARTRAM[address - 0x8F00] = value | 0xF0;

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Phoenix(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(-1);
	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Phoenix(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(-1);
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Blizzard_32K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(thisptr->mCartBank >= 0 && thisptr->mCartBank < 3 ? thisptr->mCartBank + 1 : -1);
	return thisptr->mpMemMan->ReadFloatingDataBus();
}

bool ATCartridgeEmulator::WriteByte_CCTL_Blizzard_32K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(thisptr->mCartBank >= 0 && thisptr->mCartBank < 3 ? thisptr->mCartBank + 1 : -1);
	return true;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_AddressToBank(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & T_Mask);
	return true;
}

template<uint8 T_Mask>
sint32 ATCartridgeEmulator::ReadByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : address & T_Mask);
	return 0xFF;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : address & T_Mask);
	return true;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_DataToBank(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(value & T_Mask);
	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_XEGS_64K_Alt(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(value & 0x08 ? value & 0x07 : -1);
	return true;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_DataToBank_Switchable(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(value & 0x80 ? -1 : value & T_Mask);
	return true;
}

template<uint8 T_Mask>
sint32 ATCartridgeEmulator::ReadByte_CCTL_Williams(void *thisptr0, uint32 address) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : address & T_Mask);
	return 0xFF;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_Williams(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : address & T_Mask);
	return true;
}

template<uint8 T_Address, bool T_PassThrough>
bool ATCartridgeEmulator::WriteByte_CCTL_SDX64(void *thisptr0, uint32 address, uint8 value) {
	if (((uint8)address & 0xF0) == T_Address) {
		((ATCartridgeEmulator *)thisptr0)->SetCartBank(
			address & 8
				? (address & 4) || !T_PassThrough ? -1 : 8
				: ~address & 7
		);
		return true;
	}

	return false;
}

template<bool T_PassThrough>
bool ATCartridgeEmulator::WriteByte_CCTL_SDX128(void *thisptr0, uint32 address, uint8 value) {
	if (((uint8)address & 0xE0) == 0xE0) {
		((ATCartridgeEmulator *)thisptr0)->SetCartBank(
			address & 8
				? (address & 4) || !T_PassThrough ? -1 : 16
				: (~address & 7) + (address & 0x10 ? 0 : 8)
		);
		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address < 0xD520) {
		if (address < 0xD510)
			thisptr->SetCartBank(address & 15);
		else
			thisptr->SetCartBank(-1);

		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address < 0xD520) {
		if (address < 0xD510)
			thisptr->SetCartBank(address & 15);
		else
			thisptr->SetCartBank(-1);

		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address >= 0xD520 && address < 0xD540) {
		if (address & 0x10)
			thisptr->SetCartBank(-1);
		else
			thisptr->SetCartBank(address & 15);

		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address >= 0xD520 && address < 0xD540) {
		if (address & 0x10)
			thisptr->SetCartBank(-1);
		else
			thisptr->SetCartBank(address & 15);

		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address) {
	// MaxFlash8: CCTL reads return open bus ($FF) WITHOUT switching the bank.
	// Bank selection happens only on a WRITE to $D500-$D5FF (see WriteByte
	// below).  This matches the sim816 reference model (hw.c sim_cctl), which
	// RapidusOS/SDX relies on: it probes $D500-$D503 by reading and must not
	// have the cart bank/enable state disturbed by that probe.  (Deviation from
	// the AtariMax read-latch hardware is intentional and user-approved — see
	// memory/sota-hang.md "CONFIRMED ROOT CAUSE".)
	(void)address;
	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : (uint8)address & 0x7F);
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_SIC(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint8 addr8 = (uint8)address;

	if (addr8 >= 0x20)
		return -1;

	return (uint8)thisptr->mCartBank;
}

bool ATCartridgeEmulator::WriteByte_CCTL_SIC(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if ((uint8)address < 0x20)
		thisptr->SetCartBank(value);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_SICPlus(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint8 addr8 = (uint8)address;

	if (addr8 >= 0x40)
		return -1;

	return (uint8)thisptr->mCartBank;
}

bool ATCartridgeEmulator::WriteByte_CCTL_SICPlus(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if ((uint8)address < 0x40)
		thisptr->SetCartBank(value);

	return true;
}

// The!Cart registers:
//
// $D5A0: primary bank register low byte (0-255, default: 0)
// $D5A1: primary bank register high byte (0-63, default: 0)
// $D5A2: primary bank enable (1=enable, 0=disable, default: 1)
// $D5A3: secondary bank register low byte (0-255, default: 0)
// $D5A4: secondary bank register high byte (0-63, default: 0)
// $D5A5: secondary bank enable (1=enable, 0=disable, default: 0)
// $D5A6: cart mode select (see section on cartridge modes, default: 1 / 8k)
// $D5A7: flash/ram selection and write enable control (0-15, default: 0)
//   bit 0: primary bank write enable (0 = write protect, 1 = write enable)
//   bit 1: primary bank source (0 = flash, 1 = RAM)
//   bit 2: secondary bank write enable (0 = write protect, 1 = write enable)
//   bit 3: secondary bank source (0 = flash, 1 = RAM)
// 
// $D5A8: SPI interface to EEPROM
//   bit 0: SPI CLK
//   bit 1: SPI CS
//   bit 7: SPI data in (on reads), SPI data out (on writes)

sint32 ATCartridgeEmulator::ReadByte_CCTL_TheCart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;
	address &= 0xff;

	if (address >= 0xA0 && address <= 0xAF && !thisptr->mbTheCartConfigLock) {
		if (address <= 0xA7)
			return thisptr->mTheCartRegs[address - 0xA0];
		else if (address == 0xA8)
			return (thisptr->mTheCartRegs[8] & 0x03) + (thisptr->mEEPROM.ReadState() ? 0x80 : 0x00);
		else
			return 0xFF;
	} else if (thisptr->mTheCartBankMode == kTheCartBankMode_SIC && address < 0x20) {
		return ((thisptr->mTheCartRegs[0] >> 1) & 0x1F) + (thisptr->mTheCartRegs[2] ? 0 : 0x40) + (thisptr->mTheCartRegs[5] ? 0x20 : 0);
	} else if (thisptr->mbTheCartBankByAddress) {
		WriteByte_CCTL_TheCart(thisptr0, address, 0);
		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_TheCart(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;
	address &= 0xff;

	if (address >= 0xA0 && address <= 0xAF && !thisptr->mbTheCartConfigLock) {
		if (address <= 0xA7) {
			static const uint8 kRegWriteMasks[]={
				0xFF,
				0x3F,
				0x01,
				0xFF,
				0x3F,
				0x01,
				0x3F,
				0x0F,
			};

			const int index = address - 0xA0;
			uint8& reg = thisptr->mTheCartRegs[index];

			value &= kRegWriteMasks[index];

			bool forceUpdate = false;

			switch(index) {
				case 0:
				case 1:
					// accessing the primary bank registers also enables the primary window
					if (!thisptr->mTheCartRegs[2]) {
						thisptr->mTheCartRegs[2] = 1;
						forceUpdate = true;
					}
					break;

				case 3:
				case 4:
					// accessing the secondary bank registers also enables the secondary window
					if (!thisptr->mTheCartRegs[5]) {
						thisptr->mTheCartRegs[5] = 1;
						forceUpdate = true;
					}
					break;
			}

			if (forceUpdate || reg != value) {
				const uint8 delta = reg ^ value;
				reg = value;

				// check if we updated the banking mode register -- this is particularly expensive
				if (index == 6)
					thisptr->UpdateTheCartBanking();
				
				if (index == 7) {
					// Invalidate and recreate banking if we have an R/W state change:
					// bit 0 controls primary window read/write
					// bit 2 controls secondary window read/write

					if (delta & 0x01)
						thisptr->mCartBank = -1;

					if (delta & 0x04)
						thisptr->mCartBank2 = -1;
				}

				// must come after banking update
				thisptr->UpdateTheCart();
			}
		} else if (address == 0xA8) {
			value &= 0x83;
			
			if (thisptr->mTheCartRegs[8] != value) {
				thisptr->mTheCartRegs[8] = value;

				thisptr->mEEPROM.WriteState((value & 2) == 0, (value & 1) != 0, (value & 0x80) != 0);
			}
		} else if (address == 0xAF) {
			thisptr->mbTheCartConfigLock = true;
		}

		return true;
	} else {
		const sint16 bankInfo = thisptr->mTheCartBankInfo[thisptr->mbTheCartBankByAddress ? address : value];

		if (bankInfo >= 0) {
			// modify primary bank register
			const uint16 oldBank = VDReadUnalignedLEU16(thisptr->mTheCartRegs);
			const uint16 newBank = oldBank ^ ((oldBank ^ (uint16)bankInfo) & (thisptr->mTheCartBankMask | 0x4000));
			const uint8 newEnable = bankInfo & 0x4000 ? 1 : 0;

			// argh... we could almost get away with a generic routine here, if it weren't
			// for SIC! and OSS. :(

			switch(thisptr->mTheCartBankMode) {
				case kTheCartBankMode_SIC:
					if (address < 0x20) {
						const uint8 newEnables = (bankInfo & 0x6000) >> 8;
						if (oldBank != newBank || thisptr->mTheCartSICEnables != newEnables) {
							VDWriteUnalignedLEU16(thisptr->mTheCartRegs, newBank);
							thisptr->mTheCartSICEnables = newEnables;

							thisptr->UpdateTheCart();
						}
					}
					break;

				case kTheCartBankMode_OSS:
					if (thisptr->mTheCartOSSBank != (uint8)bankInfo) {
						thisptr->mTheCartOSSBank = (uint8)bankInfo;

						thisptr->UpdateTheCart();
					}
					break;

				default:
					if (oldBank != newBank || thisptr->mTheCartRegs[2] != newEnable) {
						VDWriteUnalignedLEU16(thisptr->mTheCartRegs, newBank);
						thisptr->mTheCartRegs[2] = newEnable;

						thisptr->UpdateTheCart();
					}
					break;
			}

			return true;
		}
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_SC3D(void *thisptr0, uint32 address) {
	return ((ATCartridgeEmulator *)thisptr0)->mSC3D[address & 3];
}

bool ATCartridgeEmulator::WriteByte_CCTL_SC3D(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// Information on how the SuperCharger 3D cart works comes from jindroush, by way of
	// HiassofT:
	//
	//	0,1,2 are data regs, 3 is command/status.
	//
	//	Command 1, division:
	//	reg3 = 1
	//	reg1 (hi) reg2 (lo) / reg0 = reg2 (res), reg1 (remainder).
	//
	//	If there's error, status is 1, otherwise status is 0.
	//
	//	Command 2, multiplication:
	//	reg3 = 2
	//	reg2 * reg0 = reg1 (hi), reg2 (lo).

	int idx = (int)address & 3;

	if (idx < 3)
		thisptr->mSC3D[idx] = value;
	else {
		if (value == 1) {
			uint32 d = ((uint32)thisptr->mSC3D[1] << 8) + (uint32)thisptr->mSC3D[2];

			if (thisptr->mSC3D[1] >= thisptr->mSC3D[0]) {
				thisptr->mSC3D[3] = 1;
			} else {
				thisptr->mSC3D[2] = (uint8)(d / (uint32)thisptr->mSC3D[0]);
				thisptr->mSC3D[1] = (uint8)(d % (uint32)thisptr->mSC3D[0]);
				thisptr->mSC3D[3] = 0;
			}
		} else if (value == 2) {
			uint32 result = (uint32)thisptr->mSC3D[2] * (uint32)thisptr->mSC3D[0];

			thisptr->mSC3D[1] = (uint8)(result >> 8);
			thisptr->mSC3D[2] = (uint8)result;
			thisptr->mSC3D[3] = 0;
		} else {
			thisptr->mSC3D[3] = 1;
		}
	}

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_TelelinkII(void *thisptr0, uint32 address) {
	if (address & 1) {
		ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

		// initiate array load
		for(int i=0; i<256; ++i)
			thisptr->mCARTRAM[i] = thisptr->mpROM[8192 + i] | 0xF0;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_TelelinkII(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// initiate NV store
	memcpy(thisptr->mpROM + 8192, thisptr->mCARTRAM.data(), 256);
	thisptr->mbDirty = true;
	thisptr->mpImage->SetDirty();

	if (thisptr->mpUIRenderer)
		thisptr->mpUIRenderer->SetFlashWriteActivity();

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_034M(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 1, 2, 6, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_034M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 1, 2, 6, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_043M(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 2, 1, 6, 4, 2, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_043M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 2, 1, 6, 4, 2, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_M091(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[16] = {1, 3, 1, 3, 1, 3, 1, 3, -1, 2};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_M091(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[16] = {1, 3, 1, 3, 1, 3, 1, 3, -1, 2};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_8K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[10] = {1, 1, 1, 1, 1, 1, 1, 1, -1, 0};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_8K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[10] = {1, 1, 1, 1, 1, 1, 1, 1, -1, 0};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MDDOS(void *thisptr0, uint32 address) {
	WriteByte_CCTL_MDDOS(thisptr0, address, 0);
	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MDDOS(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	static const sint8 kBankLookup[16] = {1, 3, 3, 3, 3, 3, 3, 3, -1, 2};
	int bank = kBankLookup[address & 9];
	
	if (bank >= 0)
		bank += (~address & 0x30) >> 2;

	thisptr->SetCartBank(bank);
	thisptr->SetCartBank2(bank >= 0 ? bank & 12 : -1);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_COS32K(void *thisptr0, uint32 address) {
	WriteByte_CCTL_COS32K(thisptr0, address, 0);
	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_COS32K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(0);

	// Strobing /CCTL pulls A14 high with an RC circuit with C=1nF and R=10K.
	// TTL threshold is between 0.8V and 2.0V, so splitting the difference at
	// 1.4V gives a reset time of -ln(1.4V/5V)*RC = 12.73us, or ~23 cycles.
	thisptr->mpScheduler->SetEvent(23, thisptr, 1, thisptr->mpEventReset);

	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Corina(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address != 0xD500)
		return false;

	int bank = -1;

	// D7=1 disables cartridge.
	// D7=0 enables cartridge.
	if (!(value & 0x80)) {
		switch(value & 0x60) {
			case 0x00:	// 000xxxxx -> ROM (banks 0-31)
			case 0x20:	// 001xxxxx -> ROM/SRAM (banks 32-63)
				bank = value;
				break;
			case 0x40:	// 10xxxxxx -> EEPROM
				bank = 64;
				break;
			case 0x60:	// 11xxxxxx -> reserved
				bank = -1;
				break;
		}
	}

	thisptr->SetCartBank(bank);
	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_AST_32K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(-1);
	thisptr->SetCartBank2((thisptr->mCartBank2 + 1) & 127);
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 7));
	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 7));
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 15));
	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 15));
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_5200_512K_32KBanks(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint8 addr8 = (uint8)address;
	const uint8 bankMask = (uint8)((thisptr->mCartSize - 1) >> 15);

	if (addr8 >= 0xE0)
		thisptr->SetCartBank(bankMask);
	else if (addr8 >= 0xD0)
		thisptr->SetCartBank(bankMask & ((thisptr->mCartBank & 0x0C) + ((address >> 2) & 0x03)));
	else if (addr8 >= 0xC0)
		thisptr->SetCartBank(bankMask & ((thisptr->mCartBank & 0x03) + ((address >> 0) & 0x0C)));

	return thisptr->mpROM[address - 0x4000 + (thisptr->mCartBank << 15)];
}

bool ATCartridgeEmulator::WriteByte_CCTL_5200_512K_32KBanks(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	uint8 addr8 = (uint8)address;
	const uint8 bankMask = (uint8)((thisptr->mCartSize - 1) >> 15);

	if (addr8 >= 0xE0)
		thisptr->SetCartBank(bankMask);
	else if (addr8 >= 0xD0)
		thisptr->SetCartBank(bankMask & ((thisptr->mCartBank & 0x0C) + ((address >> 2) & 0x03)));
	else if (addr8 >= 0xC0)
		thisptr->SetCartBank(bankMask & ((thisptr->mCartBank & 0x03) + ((address >> 0) & 0x0C)));

	return true;
}

namespace {
	static const sint8 kMicroCalcTab[]={ 0, 1, 2, 3, -1 };
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MicroCalc(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(kMicroCalcTab[thisptr->mCartBank + 1]);

	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MicroCalc(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(kMicroCalcTab[thisptr->mCartBank + 1]);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MegaCart3(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	return (uint8)thisptr->mCartBank;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MegaCart3_512K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(value & 0x80 ? -1 : value & 0x1F);

	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MegaCart3_4M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(value == 0xFF ? -1 : value);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_aDawliah_32K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((thisptr->GetCartBank() + 1) & 3);
	return (uint8)thisptr->mpMemMan->ReadFloatingDataBus();
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_aDawliah_64K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((thisptr->GetCartBank() + 1) & 7);
	return (uint8)thisptr->mpMemMan->ReadFloatingDataBus();
}

bool ATCartridgeEmulator::WriteByte_CCTL_aDawliah_32K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((thisptr->GetCartBank() + 1) & 3);
	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_aDawliah_64K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((thisptr->GetCartBank() + 1) & 7);
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_JRC_RAMBOX(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	if ((uint8)address >= 0x80)
		return thisptr->mCARTRAM[((uint32)thisptr->mCartBank2 << 7) + (address & 0x7F)];

	return -1;
}
bool ATCartridgeEmulator::WriteByte_CCTL_JRC_RAMBOX(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;
	const uint8 addr8 = (uint8)address;

	// See also: https://atariwiki.org/wiki/Wiki.jsp?page=Cartridges#section-Cartridges-JRCCartridges (old)
	//           https://a8.jindrou.sh/acdp_type_jrc64.html (more recent/accurate info)

	if (addr8 >= 0x80) {
		thisptr->mCARTRAM[((uint32)thisptr->mCartBank2 << 7) + (address & 0x7F)] = value;
	} else {
		if (value & 0x80) {
			// D7=1 disables ROM access and drops RD5, disabling the cartridge. CCTL and RAM
			// are not disabled. D4-D6 are latched but irrelevant, as the button is the only
			// way to re-activate the cartridge and that clears the latch.
			thisptr->SetCartBank(-1);
		} else {
			// D4-D6 are latched for ROM A13-A15. These are in natural order, unlike the JRC6 64K cart.
			// D0-D3 are latched for RAM A14-A17.
			// A0-A6 are latched for RAM A7-A13.
			thisptr->SetCartBank((value >> 4) & 7);
			thisptr->SetCartBank2(addr8 + ((value & 15) << 7));
		}
	}

	return false;
}

bool ATCartridgeEmulator::WriteByte_CCTL_JRC6_64K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// See also: https://atariwiki.org/wiki/Wiki.jsp?page=Cartridges#section-Cartridges-JRCCartridges (old)
	//           https://a8.jindrou.sh/acdp_type_jrc64.html (more recent/accurate info)

	if (value & 0x80) {
		// D7=1 disables both /CCTL and ROM access and drops RD5, disabling the
		// cartridge. D4-D6 are latched but irrelevant, as the button is the only
		// way to re-activate the cartridge and that clears the latch.
		thisptr->SetCartBank(-1);
	} else {
		// D4-D6 inverted drive ROM A15-A13, mapped backwards: /D4 -> D15,
		// /D5 -> A14, /D6 -> A13.

		static constexpr uint8 kBankLookup[] = { 7, 3, 5, 1, 6, 2, 4, 0 };
		thisptr->SetCartBank(kBankLookup[(value >> 4) & 7]);
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_XEMulticart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// The XE Multicart switches banks on any access to CCTL (/CCTL strobe), but reads
	// the data bus -- which means that on a read it will latch the floating data bus
	// into the bank latch.
	const uint8 v = thisptr->mpMemMan->ReadFloatingDataBus();

	thisptr->SetCartBank(v);

	return v;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Pronto(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// The Pronto cartridge reads from $D501 to trigger a recall on the 256x4
	// EEPROM and writes to $D502 to trigger a store. We're just going to assume
	// for now that the cart doesn't actually decode any address lines for CCTL,
	// as that'd largely be unnecessary.
	// 
	for(int i=0; i<256; ++i)
		thisptr->mCARTRAM[i] = thisptr->mpROM[0xF00 + i] | 0xF0;

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Pronto(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;
	bool changed = false;

	for(int i=0; i<256; ++i) {
		if ((thisptr->mpROM[0xF00 + i] ^ thisptr->mCARTRAM[i]) & 15) {
			thisptr->mpROM[0xF00 + i] = thisptr->mCARTRAM[i] | 0xF0;
			changed = true;
		}
	}

	if (changed) {
		thisptr->mbDirty = true;
		thisptr->mpImage->SetDirty();

		if (thisptr->mpUIRenderer)
			thisptr->mpUIRenderer->SetFlashWriteActivity();
	}

	return true;
}

sint32 ATCartridgeEmulator::DebugReadByte_CCTL_DCart(void *thisptr0, uint32 address) {
	const ATCartridgeEmulator *const thisptr = (const ATCartridgeEmulator *)thisptr0;

	// read from 0x1500 within current bank, regardless of whether cart window is enabled
	const uint32 flashOffset = address - 0xD500 + 0x1500 + ((uint32)(thisptr->mCartBank & 0x3F) << 13);
	if (thisptr->mFlashEmu.IsControlReadEnabled()) {
		uint8 v = 0;
		thisptr->mFlashEmu.DebugReadByte(flashOffset, v);

		return v;
	} else
		return thisptr->mpROM[flashOffset];
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_DCart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// read from 0x1500 within current bank, regardless of whether cart window is enabled
	const uint32 flashOffset = address - 0xD500 + 0x1500 + ((uint32)(thisptr->mCartBank & 0x3F) << 13);
	if (thisptr->mFlashEmu.IsControlReadEnabled()) {
		uint8 v = 0;
		thisptr->mFlashEmu.ReadByte(flashOffset, v);
		return v;
	} else
		return thisptr->mpROM[flashOffset];
}

///////////////////////////////////////////////////////////////////////////

void ATCartridgeEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('C', 'A', 'R', 'T'), this, &ATCartridgeEmulator::LoadStatePrivate);
}

void ATCartridgeEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	ExchangeState(reader);
}

void ATCartridgeEmulator::EndLoadState(ATSaveStateReader& reader) {
	UpdateCartBank();
	UpdateCartBank2();
}

class ATSaveStateCartridge : public ATSnapExchangeObject<ATSaveStateCartridge, "ATSaveStateCartridge"> {
public:
	template<typename T>
	void Exchange(T& rw) {
		rw.Transfer("bank", &mBank);
		rw.Transfer("bank2", &mBank2);
		rw.Transfer("image_crc32", &mImageCrc32);
		rw.Transfer("image_crc32_valid", &mImageCrc32Valid);
		rw.Transfer("rom_crc32", &mRomCrc32);
		rw.Transfer("ram", &mpRam);
	}

	sint32 mBank {};
	sint32 mBank2 {};
	uint32 mImageCrc32 {};
	bool mImageCrc32Valid {};
	uint32 mRomCrc32 {};
	vdrefptr<ATSaveStateMemoryBuffer> mpRam;
};

void ATCartridgeEmulator::LoadState(IATObjectState& state) {
	auto& cartState = atser_cast<const ATSaveStateCartridge&>(state);
	const auto& cartInfo = kATCartridgeModeTable.table[mCartMode];

	const sint32 clampedBank = std::clamp(cartState.mBank, cartInfo.mBank1Min, cartInfo.mBank1Max);
	const sint32 clampedBank2 = std::clamp(cartState.mBank2, cartInfo.mBank2Min, cartInfo.mBank2Max);

	VDASSERT(clampedBank == cartState.mBank);
	VDASSERT(clampedBank2 == cartState.mBank2);

	SetCartBank(clampedBank);
	SetCartBank2(clampedBank2);

	if (!mCARTRAM.empty()) {
		const size_t len = mCARTRAM.size() * sizeof(mCARTRAM[0]);

		memset(mCARTRAM.data(), 0xFF, len);

		if (cartState.mpRam) {
			const auto& readBuffer = cartState.mpRam->GetReadBuffer();
			memcpy(mCARTRAM.data(), readBuffer.data(), std::min<size_t>(len, readBuffer.size()));
		}
	}
}

void ATCartridgeEmulator::SaveState(IATObjectState **pp) {
	vdrefptr<ATSaveStateCartridge> obj(new ATSaveStateCartridge);

	obj->mBank = mCartBank;
	obj->mBank2 = mCartBank2;

	const auto imagecrc = GetImageFileCRC();
	obj->mImageCrc32Valid = imagecrc.has_value();
	obj->mImageCrc32 = imagecrc.value_or(0);

	obj->mRomCrc32 = VDCRCTable::CRC32.CRC(mpROM, mCartSize);

	if (!mCARTRAM.empty()) {
		vdrefptr<ATSaveStateMemoryBuffer> savedRam(new ATSaveStateMemoryBuffer);
		savedRam->mpDirectName = L"cart-ram.bin";

		const size_t len = mCARTRAM.size() * sizeof(mCARTRAM[0]);
		auto& writeBuffer = savedRam->GetWriteBuffer();
		writeBuffer.resize(len);
		memcpy(writeBuffer.data(), mCARTRAM.data(), len);

		obj->mpRam = std::move(savedRam);
	}

	*pp = obj.release();
}

uint8 ATCartridgeEmulator::DebugReadLinear(uint32 offset) const {
	if (!mpROM || offset >= mCartSize)
		return 0;

	return mpROM[offset];
}

uint8 ATCartridgeEmulator::DebugReadBanked(uint32 globalAddress) const {
	// Only base addresses between $8000-BFFF are cartridge mapped.
	if ((globalAddress & 0xFFFF) - 0x8000 >= 0x4000)
		return 0;

	sint32 baseOffset = mDebugBankMap[(globalAddress >> 16) & 0xFF][(globalAddress >> 12) & 3];

	if (baseOffset < 0)
		return 0;

	return mpROM[baseOffset + (globalAddress & 0xFFF)];
}

void ATCartridgeEmulator::InitCartridge(IATDeviceCartridgePort *cartPort) {
	mpCartridgePort = cartPort;
}

bool ATCartridgeEmulator::IsLeftCartActive() const {
	switch(mCartMode) {
		case kATCartridgeMode_SIC_128K:
		case kATCartridgeMode_SIC_256K:
		case kATCartridgeMode_SIC_512K:
		case kATCartridgeMode_SICPlus:
			return !(mCartBank & 0x40);

		default:
			break;
	}

	return mCartMode && mCartBank >= 0;
}

void ATCartridgeEmulator::SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) {
	if (mbRD4Gate == rightEnable && mbRD5Gate == leftEnable && mbCCTLGate == cctlEnable)
		return;

	mbRD4Gate = rightEnable;
	mbRD5Gate = leftEnable;
	mbCCTLGate = cctlEnable;

	UpdateLayerMasks();
}

void ATCartridgeEmulator::UpdateCartSense(bool leftActive) {
}

uint32 ATCartridgeEmulator::GetSupportedButtons() const {
	switch(mCartMode) {
		case kATCartridgeMode_COS32K:
			return UINT32_C(1) << kATDeviceButton_CartridgeSwitch;

		case kATCartridgeMode_JRC_RAMBOX:
		case kATCartridgeMode_JRC6_64K:
		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			return UINT32_C(1) << kATDeviceButton_CartridgeResetBank;

		default:
			return 0;
	}
}

bool ATCartridgeEmulator::IsButtonDepressed(ATDeviceButton idx) const {
	if (idx == kATDeviceButton_CartridgeSwitch)
		return mCartMode == kATCartridgeMode_COS32K && mbCartEnabled;

	return false;
}

void ATCartridgeEmulator::ActivateButton(ATDeviceButton idx, bool state) {
	if (idx == kATDeviceButton_CartridgeSwitch) {
		if (mbCartEnabled != state) {
			mbCartEnabled = state;

			if (mCartMode == kATCartridgeMode_COS32K)
				UpdateCartBank();
		}
	} else if (idx == kATDeviceButton_CartridgeResetBank && state) {
		switch(mCartMode) {
			case kATCartridgeMode_JRC_RAMBOX:
			case kATCartridgeMode_JRC6_64K:
				SetCartBank(mInitialCartBank);
				SetCartBank2(mInitialCartBank2);
				break;

			case kATCartridgeMode_TheCart_32M:
			case kATCartridgeMode_TheCart_64M:
			case kATCartridgeMode_TheCart_128M:
				ColdReset();
				break;
		}
	}
}

void ATCartridgeEmulator::OnScheduledEvent(uint32 id) {
	mpEventReset = nullptr;

	if (mCartMode == kATCartridgeMode_COS32K)
		SetCartBank(1);
}

template<class T>
void ATCartridgeEmulator::ExchangeState(T& io) {
	io != mCartBank;
	io != mCartBank2;
}

void ATCartridgeEmulator::InitMemoryLayers() {
	uint32 fixedBase = 0;
	uint32 fixedSize = 0;
	uint32 fixedOffset = 0;
	uint32 fixedMask = 0;
	uint32 fixed2Base = 0;
	uint32 fixed2Size = 0;
	uint32 fixed2Offset = 0;
	int fixed2Mask = -1;
	bool fixed2RAM = false;
	uint32 bank1Base = 0;
	uint32 bank1Size = 0;
	sint32 bank1Mask = -1;
	uint32 bank2Base = 0;
	uint32 bank2Size = 0;
	uint32 spec1Base = 0;
	uint32 spec1Size = 0;
	bool spec1ReadEnabled = false;
	bool spec1WriteEnabled = false;
	bool spec2Enabled = false;
	uint32 spec2Base = 0;
	uint32 spec2Size = 0;
	bool usecctl = false;
	bool usecctlread = false;
	bool usecctlwrite = false;

	ATMemoryHandlerTable spec1hd = {};
	spec1hd.mbPassAnticReads = true;
	spec1hd.mbPassReads = true;
	spec1hd.mbPassWrites = true;
	spec1hd.mpThis = this;

	ATMemoryHandlerTable spec2hd = {};
	spec2hd.mbPassAnticReads = true;
	spec2hd.mbPassReads = true;
	spec2hd.mbPassWrites = true;
	spec2hd.mpThis = this;

	ATMemoryHandlerTable cctlhd = {};
	cctlhd.mbPassAnticReads = true;
	cctlhd.mbPassReads = true;
	cctlhd.mbPassWrites = true;
	cctlhd.mpThis = this;

	const auto& modeInfo = kATCartridgeModeTable.table[mCartMode];

	if (modeInfo.mFixed1Size) {
		fixedBase = modeInfo.mFixed1Base;
		fixedSize = modeInfo.mFixed1Size;
		fixedMask = modeInfo.mFixed1Mask == 0xFF ? UINT32_C(0)-1 : modeInfo.mFixed1Mask;
		fixedOffset = modeInfo.mFixed1Offset;
	}

	if (modeInfo.mFixed2Size) {
		fixed2Base = modeInfo.mFixed2Base;
		fixed2Size = modeInfo.mFixed2Size;
		fixed2Mask = modeInfo.mFixed2Mask == 0xFF ? UINT32_C(0)-1 : modeInfo.mFixed2Mask;
		fixed2Offset = modeInfo.mFixed2Offset;
	}

	if (modeInfo.mBank1Size) {
		bank1Base = modeInfo.mBank1Base;
		bank1Size = modeInfo.mBank1Size;
		bank1Mask = modeInfo.mBank1Mask == 0xFF ? UINT32_C(0)-1 : modeInfo.mBank1Mask;
	}

	if (modeInfo.mBank2Size) {
		bank2Base = modeInfo.mBank2Base;
		bank2Size = modeInfo.mBank2Size;
	}

	if (modeInfo.mUseCctl) {
		usecctl = true;
		usecctlread = modeInfo.mUseCctlRead;
		usecctlwrite = modeInfo.mUseCctlWrite;
		cctlhd.mpDebugReadHandler = modeInfo.mpCctlDebugRead;
		cctlhd.mpReadHandler = modeInfo.mpCctlRead;
		cctlhd.mpWriteHandler = modeInfo.mpCctlWrite;
	}

	if (modeInfo.mSpec1Size) {
		spec1Base = modeInfo.mSpec1Base;
		spec1Size = modeInfo.mSpec1Size;
		spec1hd.mpDebugReadHandler = modeInfo.mpSpec1DebugRead;
		spec1hd.mpReadHandler = modeInfo.mpSpec1Read;
		spec1hd.mpWriteHandler = modeInfo.mpSpec1Write;
	}

	switch(mCartMode) {
		case kATCartridgeMode_SuperCharger3D:
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_SC3D;
			cctlhd.mpReadHandler = ReadByte_CCTL_SC3D;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SC3D;
			break;

		case kATCartridgeMode_BountyBob5200:
			bank1Base	= 0x40;
			bank1Size	= 0x10;
			bank2Base	= 0x50;
			bank2Size	= 0x10;
			fixedBase	= 0x80;
			fixedSize	= 0x40;
			fixedOffset	= 0x8000;
			fixedMask	= 0x1F;
			spec1Base	= 0x4F;
			spec1Size	= 0x01;
			spec2Base	= 0x5F;
			spec2Size	= 0x01;
			spec1hd.mpDebugReadHandler = NULL;
			spec1hd.mpReadHandler = ReadByte_BB5200_1;
			spec1hd.mpWriteHandler = WriteByte_BB5200_1;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec2hd.mpDebugReadHandler = NULL;
			spec2hd.mpReadHandler = ReadByte_BB5200_2;
			spec2hd.mpWriteHandler = WriteByte_BB5200_2;
			spec2Enabled = true;
			break;

		case kATCartridgeMode_BountyBob5200Alt:
			bank1Base	= 0x40;
			bank1Size	= 0x10;
			bank2Base	= 0x50;
			bank2Size	= 0x10;
			fixedBase	= 0x80;
			fixedSize	= 0x40;
			fixedOffset	= 0x0000;
			fixedMask	= 0x1F;
			spec1Base	= 0x4F;
			spec1Size	= 0x01;
			spec2Base	= 0x5F;
			spec2Size	= 0x01;
			spec1hd.mpDebugReadHandler = NULL;
			spec1hd.mpReadHandler = ReadByte_BB5200_1;
			spec1hd.mpWriteHandler = WriteByte_BB5200_1;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec2hd.mpDebugReadHandler = NULL;
			spec2hd.mpReadHandler = ReadByte_BB5200_2;
			spec2hd.mpWriteHandler = WriteByte_BB5200_2;
			spec2Enabled = true;
			break;

		case kATCartridgeMode_BountyBob800:
			bank1Base	= 0x80;
			bank1Size	= 0x10;
			bank2Base	= 0x90;
			bank2Size	= 0x10;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x8000;
			spec1Base	= 0x8F;
			spec1Size	= 0x01;
			spec2Base	= 0x9F;
			spec2Size	= 0x01;
			spec1hd.mpDebugReadHandler = NULL;
			spec1hd.mpReadHandler = ReadByte_BB800_1;
			spec1hd.mpWriteHandler = WriteByte_BB800_1;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec2hd.mpDebugReadHandler = NULL;
			spec2hd.mpReadHandler = ReadByte_BB800_2;
			spec2hd.mpWriteHandler = WriteByte_BB800_2;
			spec2Enabled = true;
			break;

		case kATCartridgeMode_DB_32K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x6000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_AddressToBank<0x03>;
			break;

		case kATCartridgeMode_MegaCart_2M:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x7F>;
			break;

		case kATCartridgeMode_SpartaDosX_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = false;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX128<true>;
			break;

		case kATCartridgeMode_Atrax_SDX_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = false;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX128<false>;
			break;

		case kATCartridgeMode_SpartaDosX_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = false;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0xE0, true>;
			break;

		case kATCartridgeMode_Atrax_SDX_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = false;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0xE0, false>;
			break;

		case kATCartridgeMode_Atrax_128K:
		case kATCartridgeMode_Atrax_128K_Raw:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x0F>;
			break;

		case kATCartridgeMode_Williams_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Williams<7>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Williams<7>;
			break;

		case kATCartridgeMode_Williams_32K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Williams<3>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Williams<3>;
			break;

		case kATCartridgeMode_Williams_16K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Williams<1>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Williams<1>;
			break;

		case kATCartridgeMode_Diamond_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = false;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0xD0, false>;
			break;

		case kATCartridgeMode_Express_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = false;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0x70, false>;
			break;

		case kATCartridgeMode_TelelinkII:
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixed2Base	= 0x80;
			fixed2Size	= 0x20;
			fixed2Offset = 0;
			fixed2Mask	= 0;
			fixed2RAM	= true;
			spec1hd.mpWriteHandler = WriteByte_TelelinkII;
			spec1Base	= 0x80;
			spec1Size	= 0x20;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_TelelinkII;
			cctlhd.mpWriteHandler = WriteByte_CCTL_TelelinkII;
			break;

		case kATCartridgeMode_MaxFlash_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_128K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_128K;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;

			// 04072012 flasher expects mfr|device == 0x20 or 0x21
			{
				ATFlashType flashType = kATFlashType_Am29F010;

				if (g_ATOptions.mMaxflash1MbFlashChip == "M29F010B")
					flashType = kATFlashType_M29F010B;
				else if (g_ATOptions.mMaxflash1MbFlashChip == "SST39SF010")
					flashType = kATFlashType_SST39SF010;

				mFlashEmu.Init(mpROM, flashType, mpScheduler);
			}
			break;

		case kATCartridgeMode_MaxFlash_128K_MyIDE:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_128K_MyIDE;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_128K_MyIDE;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_MaxFlash;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;

			// 04072012 flasher expects mfr|device == 0x20 or 0x21
			mFlashEmu.Init(mpROM, kATFlashType_M29F010B, mpScheduler);
			break;

		case kATCartridgeMode_MaxFlash_1024K:
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_1024K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_1024K;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_MaxFlash;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;
			{
				ATFlashType flashType = kATFlashType_Am29F040B;

				if (g_ATOptions.mMaxflash8MbFlashChip == "BM29F040")
					flashType = kATFlashType_BM29F040;
				else if (g_ATOptions.mMaxflash8MbFlashChip == "HY29F040A")
					flashType = kATFlashType_HY29F040A;
				else if (g_ATOptions.mMaxflash8MbFlashChip == "SST39SF040")
					flashType = kATFlashType_SST39SF040;

				mFlashEmu.Init(mpROM, flashType, mpScheduler);
				mFlashEmu2.Init(mpROM + 512*1024, flashType, mpScheduler);
			}
			break;

		case kATCartridgeMode_Phoenix_8K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Phoenix;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Phoenix;
			break;

		case kATCartridgeMode_Blizzard_4K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank1Mask	= 0x0F;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Phoenix;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Phoenix;
			break;

		case kATCartridgeMode_Blizzard_16K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Phoenix;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Phoenix;
			break;

		case kATCartridgeMode_Blizzard_32K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Phoenix;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Blizzard_32K;
			break;

		case kATCartridgeMode_OSS_034M:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_034M;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_034M;
			break;

		case kATCartridgeMode_OSS_M091:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_M091;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_M091;
			break;

		case kATCartridgeMode_Corina_1M_EEPROM:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			usecctl		= true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Corina;
			spec1hd.mpWriteHandler = WriteByte_Corina1M;
			break;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Corina;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpWriteHandler = WriteByte_Corina512K;
			break;

		case kATCartridgeMode_SIC_128K:
		case kATCartridgeMode_SIC_256K:
		case kATCartridgeMode_SIC_512K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank2Base	= 0x80;
			bank2Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_SIC;
			cctlhd.mpReadHandler = ReadByte_CCTL_SIC;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SIC;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_SIC;
			spec1hd.mpReadHandler = ReadByte_SIC;
			spec1hd.mpWriteHandler = WriteByte_SIC;
			spec1WriteEnabled = true;
			spec2Base	= 0x80;
			spec2Size	= 0x20;
			spec2hd.mpDebugReadHandler = DebugReadByte_SIC;
			spec2hd.mpReadHandler = ReadByte_SIC;
			spec2hd.mpWriteHandler = WriteByte_SIC;

			if (g_ATOptions.mSICFlashChip == "MX29F040") {
				if (mCartMode == kATCartridgeMode_SIC_128K)
					mFlashEmu.Init(mpROM, kATFlashType_MX29F001T, mpScheduler);
				else if (mCartMode == kATCartridgeMode_SIC_256K)
					mFlashEmu.Init(mpROM, kATFlashType_MX29F002T, mpScheduler);
				else
					mFlashEmu.Init(mpROM, kATFlashType_MX29F040, mpScheduler);
			} else if (g_ATOptions.mSICFlashChip == "SST39SF040") {
				if (mCartMode == kATCartridgeMode_SIC_128K)
					mFlashEmu.Init(mpROM, kATFlashType_SST39SF010, mpScheduler);
				else if (mCartMode == kATCartridgeMode_SIC_256K)
					mFlashEmu.Init(mpROM, kATFlashType_SST39SF020, mpScheduler);
				else
					mFlashEmu.Init(mpROM, kATFlashType_SST39SF040, mpScheduler);
			} else {
				if (mCartMode == kATCartridgeMode_SIC_128K)
					mFlashEmu.Init(mpROM, kATFlashType_Am29F010B, mpScheduler);
				else if (mCartMode == kATCartridgeMode_SIC_256K)
					mFlashEmu.Init(mpROM, kATFlashType_Am29F002BT, mpScheduler);
				else
					mFlashEmu.Init(mpROM, kATFlashType_Am29F040B, mpScheduler);
			}

			break;

		case kATCartridgeMode_SICPlus:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank2Base	= 0x80;
			bank2Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_SICPlus;
			cctlhd.mpReadHandler = ReadByte_CCTL_SICPlus;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SICPlus;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_SICPlus;
			spec1hd.mpReadHandler = ReadByte_SICPlus;
			spec1hd.mpWriteHandler = WriteByte_SICPlus;
			spec1WriteEnabled = true;
			spec2Base	= 0x80;
			spec2Size	= 0x20;
			spec2hd.mpDebugReadHandler = DebugReadByte_SICPlus;
			spec2hd.mpReadHandler = ReadByte_SICPlus;
			spec2hd.mpWriteHandler = WriteByte_SICPlus;

			if (g_ATOptions.mSICFlashChip == "MX29F040") {
				mFlashEmu.Init(mpROM, kATFlashType_MX29F040, mpScheduler);
				mFlashEmu2.Init(mpROM + 512*1024, kATFlashType_MX29F040, mpScheduler);
			} else if (g_ATOptions.mSICFlashChip == "SST39SF040") {
				mFlashEmu.Init(mpROM, kATFlashType_SST39SF040, mpScheduler);
				mFlashEmu2.Init(mpROM + 512*1024, kATFlashType_SST39SF040, mpScheduler);
			} else {
				mFlashEmu.Init(mpROM, kATFlashType_Am29F040B, mpScheduler);
				mFlashEmu2.Init(mpROM + 512*1024, kATFlashType_Am29F040B, mpScheduler);
			}
			break;

		case kATCartridgeMode_OSS_8K:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_8K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_8K;
			break;

		case kATCartridgeMode_OSS_043M:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_043M;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_043M;
			break;

		case kATCartridgeMode_AST_32K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank1Mask	= 0x00;
			bank2Base	= 0xD5;
			bank2Size	= 0x01;
			usecctl		= true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_AST_32K;
			break;

		case kATCartridgeMode_Turbosoft_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Turbosoft_64K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Turbosoft_64K;
			break;

		case kATCartridgeMode_Turbosoft_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Turbosoft_128K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Turbosoft_128K;
			break;

		case kATCartridgeMode_MegaCart_1M_2:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x3F>;
			break;

		case kATCartridgeMode_MegaCart_512K_3:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpDebugReadHandler = DebugReadByte_MegaCart3;
			spec1hd.mpReadHandler = ReadByte_MegaCart3;
			spec1hd.mpWriteHandler = WriteByte_MegaCart3;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MegaCart3_512K;
			mFlashEmu.Init(mpROM, kATFlashType_Am29F040B, mpScheduler);
			break;

		case kATCartridgeMode_MegaCart_4M_3:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpDebugReadHandler = DebugReadByte_MegaCart3;
			spec1hd.mpReadHandler = ReadByte_MegaCart3;
			spec1hd.mpWriteHandler = WriteByte_MegaCart3;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MegaCart3_4M;
			mFlashEmu.Init(mpROM, kATFlashType_Am29F032B, mpScheduler);
			break;

		case kATCartridgeMode_5200_64K_32KBanks:
		case kATCartridgeMode_5200_128K_32KBanks:
		case kATCartridgeMode_5200_256K_32KBanks:
		case kATCartridgeMode_5200_512K_32KBanks:
			bank1Base	= 0x40;
			bank1Size	= 0x80;
			spec1Base	= 0xBF;
			spec1Size	= 0x01;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec1hd.mpReadHandler = ReadByte_CCTL_5200_512K_32KBanks;
			spec1hd.mpWriteHandler = WriteByte_CCTL_5200_512K_32KBanks;
			break;

		case kATCartridgeMode_MicroCalc:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MicroCalc;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MicroCalc;
			break;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank2Base	= 0x80;
			bank2Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_TheCart;
			cctlhd.mpReadHandler = ReadByte_CCTL_TheCart;
			cctlhd.mpWriteHandler = WriteByte_CCTL_TheCart;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_TheCart;
			spec1hd.mpReadHandler = ReadByte_TheCart;
			spec1hd.mpWriteHandler = WriteByte_TheCart;
			spec1WriteEnabled = true;
			spec2Base	= 0x80;
			spec2Size	= 0x20;
			spec2hd.mpDebugReadHandler = DebugReadByte_TheCart;
			spec2hd.mpReadHandler = ReadByte_TheCart;
			spec2hd.mpWriteHandler = WriteByte_TheCart;

			switch(mCartMode) {
				case kATCartridgeMode_TheCart_32M:
					mFlashEmu.Init(mpROM, kATFlashType_S29GL256P, mpScheduler);
					break;
				case kATCartridgeMode_TheCart_64M:
					mFlashEmu.Init(mpROM, kATFlashType_S29GL512P, mpScheduler);
					break;
				case kATCartridgeMode_TheCart_128M:
					mFlashEmu.Init(mpROM, kATFlashType_S29GL01P, mpScheduler);
					break;
			}

			mEEPROM.Init();
			break;

		case kATCartridgeMode_MegaMax_2M:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_AddressToBank_Switchable<0x7F>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_AddressToBank_Switchable<0x7F>;
			break;

		case kATCartridgeMode_aDawliah_32K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_aDawliah_32K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_aDawliah_32K;
			break;

		case kATCartridgeMode_aDawliah_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_aDawliah_64K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_aDawliah_64K;
			break;

		case kATCartridgeMode_JAtariCart_128K:
		case kATCartridgeMode_JAtariCart_256K:
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_MaxFlash;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;

			switch(mCartMode) {
				case kATCartridgeMode_JAtariCart_128K:
					mFlashEmu.Init(mpROM, kATFlashType_SST39SF010, mpScheduler);
					break;
				case kATCartridgeMode_JAtariCart_256K:
					mFlashEmu.Init(mpROM, kATFlashType_SST39SF020, mpScheduler);
					break;
			}
			break;

		case kATCartridgeMode_JAtariCart_512K:
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_JAtariCart_512K;
			spec1hd.mpReadHandler = ReadByte_JAtariCart_512K;
			spec1hd.mpWriteHandler = WriteByte_JAtariCart_512K;
			spec1WriteEnabled = true;

			mFlashEmu.Init(mpROM, kATFlashType_SST39SF040, mpScheduler);
			break;

		case kATCartridgeMode_JAtariCart_1024K:
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_MaxFlash;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;
			{
				mFlashEmu.Init(mpROM, kATFlashType_SST39SF040, mpScheduler);
				mFlashEmu2.Init(mpROM + 512*1024, kATFlashType_SST39SF040, mpScheduler);
			}
			break;

		case kATCartridgeMode_DCart:
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpDebugReadHandler = DebugReadByte_MaxFlash;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;

			mFlashEmu.Init(mpROM, kATFlashType_SST39SF040, mpScheduler);
			break;
	}

	if (fixedSize) {
		mpMemLayerFixedBank1 = mpMemMan->CreateLayer(mBasePriority, mpROM + fixedOffset, fixedBase, fixedSize, true);
		mpMemMan->SetLayerIoBus(mpMemLayerFixedBank1, true);
		mpMemMan->SetLayerName(mpMemLayerFixedBank1, "Cartridge fixed window 1");

		if (fixedMask)
			mpMemMan->SetLayerMemory(mpMemLayerFixedBank1, mpROM + fixedOffset, fixedBase, fixedSize, fixedMask);

		mpMemMan->EnableLayer(mpMemLayerFixedBank1, true);
	}

	if (fixed2Size) {
		uint8 *mem = fixed2RAM ? mCARTRAM.data() : mpROM;
		mpMemLayerFixedBank2 = mpMemMan->CreateLayer(mBasePriority, mem + fixed2Offset, fixed2Base, fixedSize, true);
		mpMemMan->SetLayerIoBus(mpMemLayerFixedBank2, true);
		mpMemMan->SetLayerName(mpMemLayerFixedBank2, "Cartridge fixed window 2");

		if (fixed2Mask >= 0)
			mpMemMan->SetLayerMemory(mpMemLayerFixedBank2, mem + fixed2Offset, fixed2Base, fixed2Size, fixed2Mask);

		mpMemMan->EnableLayer(mpMemLayerFixedBank2, true);
	}

	if (bank1Size) {
		mpMemLayerVarBank1 = mpMemMan->CreateLayer(mBasePriority+1, mpROM, bank1Base, bank1Size, true);
		mpMemMan->SetLayerIoBus(mpMemLayerVarBank1, true);
		mpMemMan->SetLayerName(mpMemLayerVarBank1, "Cartridge variable window 1");

		if (bank1Mask >= 0)
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mpROM, bank1Base, bank1Size, bank1Mask);
	}

	if (bank2Size) {
		mpMemLayerVarBank2 = mpMemMan->CreateLayer(mBasePriority+2, mpROM, bank2Base, bank2Size, true);
		mpMemMan->SetLayerIoBus(mpMemLayerVarBank1, true);
		mpMemMan->SetLayerName(mpMemLayerVarBank2, "Cartridge variable window 2");
	}

	if (spec1Size) {
		mpMemLayerSpec1 = mpMemMan->CreateLayer(mBasePriority+3, spec1hd, spec1Base, spec1Size);
		mpMemMan->SetLayerIoBus(mpMemLayerSpec1, true);
		mpMemMan->SetLayerName(mpMemLayerSpec1, "Cartridge special window 1");

		if (spec1ReadEnabled) {
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, true);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, true);
		}

		if (spec1WriteEnabled)
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
	}

	if (spec2Size) {
		mpMemLayerSpec2 = mpMemMan->CreateLayer(mBasePriority+4, spec2hd, spec2Base, spec2Size);
		mpMemMan->SetLayerIoBus(mpMemLayerSpec2, true);
		mpMemMan->SetLayerName(mpMemLayerSpec2, "Cartridge special window 2");

		if (spec2Enabled)
			mpMemMan->EnableLayer(mpMemLayerSpec2, true);
	}

	if (usecctl) {
		mpMemLayerControl = mpMemMan->CreateLayer(mBasePriority+5, cctlhd, 0xD5, 0x01);
		mpMemMan->SetLayerIoBus(mpMemLayerControl, true);
		mpMemMan->SetLayerName(mpMemLayerControl, "Cartridge control window 1");

		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_AnticRead, usecctlread);
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPURead, usecctlread);
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, usecctlwrite);
	}

	if (mCartMode == kATCartridgeMode_Pronto)
		mpMemMan->EnableLayer(mpMemLayerSpec1, true);

	// This doesn't make sense for non-pow2 carts, of course, but currently we only use it for pow2.
	mCartSizeMask = (uint32)mCartSize - 1;

	UpdateLayerBuses();
	UpdateLayerMasks();

	switch(mCartMode) {
		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			UpdateTheCartBanking();
			UpdateTheCart();
			break;
	}

	UpdateCartBank();

	if (mpMemLayerVarBank2)
		UpdateCartBank2();

	mpCartridgePort->OnLeftWindowChanged(mCartId, IsLeftCartActive());
}

void ATCartridgeEmulator::ShutdownMemoryLayers() {
	if (mpMemMan)
	{
#define X(layer) if (layer) { mpMemMan->DeleteLayer(layer); layer = NULL; }
		X(mpMemLayerFixedBank1)
		X(mpMemLayerFixedBank2)
		X(mpMemLayerVarBank1)
		X(mpMemLayerVarBank2)
		X(mpMemLayerSpec1)
		X(mpMemLayerSpec2)
		X(mpMemLayerControl)
#undef X
	}
}

void ATCartridgeEmulator::SetCartBank(int bank) {
	if (mCartBank == bank)
		return;

	mCartBank = bank;
	UpdateCartBank();
}

void ATCartridgeEmulator::SetCartBank2(int bank) {
	if (mCartBank2 == bank)
		return;

	mCartBank2 = bank;
	UpdateCartBank2();
}

void ATCartridgeEmulator::UpdateCartBank() {
	if (mCartMode == kATCartridgeMode_COS32K) {
		if (!mbCartEnabled) {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpCartridgePort->OnLeftWindowChanged(mCartId, false);
			return;
		}
	}

	if (mCartMode == kATCartridgeMode_SIC_128K || mCartMode == kATCartridgeMode_SIC_256K || mCartMode == kATCartridgeMode_SIC_512K) {
		mpCartridgePort->OnLeftWindowChanged(mCartId, (mCartBank & 0x40) == 0);

		const bool flashWrite = (mCartBank & 0x80) != 0;
		const bool flashRead = mFlashEmu.IsControlReadEnabled();

		if (mCartBank & 0x40) {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
		} else {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mpROM + (((uint32)mCartBank << 14) & mCartSizeMask) + 0x2000);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, flashWrite);
		}

		if (mCartBank & 0x20) {
			mpMemMan->EnableLayer(mpMemLayerVarBank2, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, mpROM + (((uint32)mCartBank << 14) & mCartSizeMask));
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPUWrite, flashWrite);
		} else {
			mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
			mpMemMan->EnableLayer(mpMemLayerSpec2, false);
		}
		return;
	}

	if (mCartMode == kATCartridgeMode_SICPlus) {
		mpCartridgePort->OnLeftWindowChanged(mCartId, (mCartBank & 0x40) == 0);

		const bool flash2 = (mCartBank & 0x80) != 0;
		const auto& flashChip = flash2 ? mFlashEmu2 : mFlashEmu;

		const bool flashRead = flashChip.IsControlReadEnabled();
		const uint8 *windowBase = mpROM + ((uint32)(mCartBank & 0x1f) << 14);

		if (flash2)
			windowBase += 512*1024;

		if (mCartBank & 0x40) {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
		} else {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, windowBase + 0x2000);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
		}

		if (mCartBank & 0x20) {
			mpMemMan->EnableLayer(mpMemLayerVarBank2, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, windowBase);
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPUWrite, true);
		} else {
			mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
			mpMemMan->EnableLayer(mpMemLayerSpec2, false);
		}
		return;
	}

	if (mCartMode == kATCartridgeMode_TheCart_32M ||
		mCartMode == kATCartridgeMode_TheCart_64M ||
		mCartMode == kATCartridgeMode_TheCart_128M)
	{
		mpCartridgePort->OnLeftWindowChanged(mCartId, mCartBank >= 0);

		if (mCartBank < 0) {
			// primary bank disabled
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
		} else {
			uint8 base = 0xA0;
			uint8 size = 0x20;

			if (mCartBank & 0x20000) {
				base = 0xB0;
				size = 0x10;
			}

			if (!(mCartBank & 0x10000)) {
				// primary bank set to flash
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mpROM + ((mCartBank << 13) & mCartSizeMask), base, size, (uint32)0-1, true);
				mpMemMan->EnableLayer(mpMemLayerVarBank1, true);

				const bool flashRead = mFlashEmu.IsControlReadEnabled();
				mpMemMan->SetLayerAddressRange(mpMemLayerSpec1, base, size);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, (mTheCartRegs[7] & 0x01) != 0);
			} else {
				// primary bank set to RAM
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTRAM.data() + ((mCartBank << 13) & 0x7ffff), base, size, (uint32)0-1, !(mTheCartRegs[7] & 0x01));
				mpMemMan->EnableLayer(mpMemLayerVarBank1, true);

				// disable flash layer
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			}
		}

		return;
	}

	if (mCartMode == kATCartridgeMode_XEGS_64K_Alt) {
		if (mCartBank >= 0) {
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mpROM + (mCartBank << 13));
			mpMemMan->EnableLayer(mpMemLayerVarBank1, true);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
		} else {
			mpMemMan->EnableLayer(mpMemLayerSpec1, true);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
		}
	}

	if (mCartMode == kATCartridgeMode_DCart) {
		if (mCartBank & 0x80) {
			mpCartridgePort->OnLeftWindowChanged(mCartId, false);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			return;
		}
	}

	if (mCartMode == kATCartridgeMode_SpartaDosX_64K) {
		if (mCartBank < 0) {
			// disable cart
			mpCartridgePort->OnLeftWindowChanged(mCartId, false);
			mpCartridgePort->EnablePassThrough(mCartId, false, false, true);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
		} else if (mCartBank < 8) {
			// enable cart
			mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, mpROM + (mCartBank << 13), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, true);
			mpCartridgePort->EnablePassThrough(mCartId, false, false, true);
			mpCartridgePort->OnLeftWindowChanged(mCartId, true);
		} else {
			// enable pass through
			mpCartridgePort->EnablePassThrough(mCartId, true, true, true);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpCartridgePort->OnLeftWindowChanged(mCartId, false);
		}

		return;
	}

	if (mCartMode == kATCartridgeMode_SpartaDosX_128K) {
		if (mCartBank < 0) {
			// disable cart
			mpCartridgePort->OnLeftWindowChanged(mCartId, false);
			mpCartridgePort->EnablePassThrough(mCartId, false, false, true);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
		} else if (mCartBank < 16) {
			// enable cart
			mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, mpROM + (mCartBank << 13), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, true);
			mpCartridgePort->EnablePassThrough(mCartId, false, false, true);
			mpCartridgePort->OnLeftWindowChanged(mCartId, true);
		} else {
			// enable pass through
			mpCartridgePort->EnablePassThrough(mCartId, true, true, true);
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpCartridgePort->OnLeftWindowChanged(mCartId, false);
		}

		return;
	}
	
	if (mCartBank < 0) {
		mpCartridgePort->OnLeftWindowChanged(mCartId, mCartMode == kATCartridgeMode_BountyBob800);

		switch(mCartMode) {
			case kATCartridgeMode_Corina_1M_EEPROM:
			case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			case kATCartridgeMode_MaxFlash_128K:
			case kATCartridgeMode_MaxFlash_128K_MyIDE:
			case kATCartridgeMode_MaxFlash_1024K:
			case kATCartridgeMode_MaxFlash_1024K_Bank0:
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
				break;

			case kATCartridgeMode_Switchable_XEGS_32K:
			case kATCartridgeMode_Switchable_XEGS_64K:
			case kATCartridgeMode_Switchable_XEGS_128K:
			case kATCartridgeMode_Switchable_XEGS_256K:
			case kATCartridgeMode_Switchable_XEGS_512K:
			case kATCartridgeMode_Switchable_XEGS_1M:
				mpMemMan->EnableLayer(mpMemLayerFixedBank1, false);
				break;

			case kATCartridgeMode_JRC6_64K:
				mpMemMan->SetLayerModes(mpMemLayerControl, kATMemoryAccessMode_0);
				break;

			default:
				break;
		}

		if (mpMemLayerVarBank1)
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
		return;
	}

	if (mpMemLayerVarBank1)
		mpMemMan->EnableLayer(mpMemLayerVarBank1, true);

	mpCartridgePort->OnLeftWindowChanged(mCartId, mCartMode != kATCartridgeMode_RightSlot_8K);

	const uint8 *cartbase = mpROM;
	switch(mCartMode) {
		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
		case kATCartridgeMode_MaxFlash_1024K:
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
		case kATCartridgeMode_JAtariCart_128K:
		case kATCartridgeMode_JAtariCart_256K:
		case kATCartridgeMode_JAtariCart_1024K:
		case kATCartridgeMode_DCart:
			// 8K banks (1 or 2 flash chips)
			mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, cartbase + (mCartBank << 13), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			break;

		case kATCartridgeMode_JAtariCart_512K:
			// 8K banks (1 flash chip + 1 empty socket)
			if (mCartBank >= 64) {
				mpMemMan->EnableLayer(mpMemLayerSpec1, true);
			} else {
				mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, cartbase + ((mCartBank << 13) & mCartSizeMask), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			}

			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			break;

		case kATCartridgeMode_SpartaDosX_64K:
		case kATCartridgeMode_SpartaDosX_128K:
		case kATCartridgeMode_JRC_RAMBOX:
			// 8K banks
			mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, cartbase + (mCartBank << 13), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			break;

		case kATCartridgeMode_JRC6_64K:
			// 8K banks with CCTL disable (write)
			mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, cartbase + (mCartBank << 13), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			mpMemMan->SetLayerModes(mpMemLayerControl, kATMemoryAccessMode_W);
			break;

		case kATCartridgeMode_DB_32K:
		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_XEGS_256K:
		case kATCartridgeMode_XEGS_512K:
		case kATCartridgeMode_XEGS_1M:
		case kATCartridgeMode_Williams_64K:
		case kATCartridgeMode_Williams_32K:
		case kATCartridgeMode_Williams_16K:
		case kATCartridgeMode_Express_64K:
		case kATCartridgeMode_Diamond_64K:
		case kATCartridgeMode_Atrax_128K:
		case kATCartridgeMode_Atrax_128K_Raw:
		case kATCartridgeMode_Atrax_SDX_64K:
		case kATCartridgeMode_Atrax_SDX_128K:
		case kATCartridgeMode_AST_32K:
		case kATCartridgeMode_Turbosoft_64K:
		case kATCartridgeMode_Turbosoft_128K:
		case kATCartridgeMode_MegaCart_1M_2:
		case kATCartridgeMode_MicroCalc:
		case kATCartridgeMode_Blizzard_32K:
		case kATCartridgeMode_aDawliah_32K:
		case kATCartridgeMode_aDawliah_64K:
		case kATCartridgeMode_JAtariCart_8K:
		case kATCartridgeMode_JAtariCart_16K:
		case kATCartridgeMode_JAtariCart_32K:
		case kATCartridgeMode_JAtariCart_64K:
			// 8K banks
			mpMemMan->SetLayerMemoryAndAddressSpace(mpMemLayerVarBank1, cartbase + (mCartBank << 13), kATAddressSpace_CB + (mCartBank << 16) + 0xA000);
			break;

		case kATCartridgeMode_MegaCart_512K_3:
			// 8K banks, masked to 512K
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + ((mCartBank & 0x1f) << 14));
			break;

		case kATCartridgeMode_Switchable_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_256K:
		case kATCartridgeMode_Switchable_XEGS_512K:
		case kATCartridgeMode_Switchable_XEGS_1M:
			// 8K banks
			mpMemMan->EnableLayer(mpMemLayerFixedBank1, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 13));
			break;

		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
		case kATCartridgeMode_MegaCart_2M:
		case kATCartridgeMode_MegaCart_4M_3:
		case kATCartridgeMode_MegaMax_2M:
		case kATCartridgeMode_COS32K:
			// 16K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14));
			break;

		case kATCartridgeMode_5200_64K_32KBanks:
		case kATCartridgeMode_5200_128K_32KBanks:
		case kATCartridgeMode_5200_256K_32KBanks:
		case kATCartridgeMode_5200_512K_32KBanks:
			// 32K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 15));
			break;

		case kATCartridgeMode_OSS_034M:
		case kATCartridgeMode_OSS_043M:
		case kATCartridgeMode_OSS_M091:
		case kATCartridgeMode_OSS_8K:
		case kATCartridgeMode_MDDOS:
		case kATCartridgeMode_BountyBob5200:
		case kATCartridgeMode_BountyBob800:
			// 4K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 12));
			break;

		case kATCartridgeMode_BountyBob5200Alt:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 12) + 0x2000);
			break;

		case kATCartridgeMode_Corina_1M_EEPROM:
			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (64 << 14), 0x80, 0x40, 0x1F);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			} else {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14), 0x80, 0x40);
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			}
			break;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (32 << 14), 0x80, 0x40, 0x1F);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			} else if (mCartBank >= 32) {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTRAM.data() + ((mCartBank - 32) << 14), 0x80, 0x40, 0xFFFFFFFFU, false);
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			} else {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14), 0x80, 0x40, 0xFFFFFFFFU, true);
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			}
			break;

		case kATCartridgeMode_XEMulticart_8K:
		case kATCartridgeMode_XEMulticart_16K:
		case kATCartridgeMode_XEMulticart_32K:
		case kATCartridgeMode_XEMulticart_64K:
		case kATCartridgeMode_XEMulticart_128K:
		case kATCartridgeMode_XEMulticart_256K:
		case kATCartridgeMode_XEMulticart_512K:
		case kATCartridgeMode_XEMulticart_1M:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + ((mCartBank << 13) & mCartSizeMask));

			if (mCartBank & 0x80) {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (((mCartBank & 0xFE) << 13) & mCartSizeMask));
				mpMemMan->EnableLayer(mpMemLayerVarBank2, true);
			} else
				mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
			break;
	}
}

void ATCartridgeEmulator::UpdateCartBank2() {
	switch(mCartMode) {
		case kATCartridgeMode_SIC_128K:
		case kATCartridgeMode_SIC_256K:
		case kATCartridgeMode_SIC_512K:
		case kATCartridgeMode_JRC6_64K:
		case kATCartridgeMode_JRC_RAMBOX:
		case kATCartridgeMode_XEMulticart_8K:
		case kATCartridgeMode_XEMulticart_16K:
		case kATCartridgeMode_XEMulticart_32K:
		case kATCartridgeMode_XEMulticart_64K:
		case kATCartridgeMode_XEMulticart_128K:
		case kATCartridgeMode_XEMulticart_256K:
		case kATCartridgeMode_XEMulticart_512K:
		case kATCartridgeMode_XEMulticart_1M:
			return;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			if (mCartBank2 < 0) {
				// secondary bank disabled
				mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
				mpMemMan->EnableLayer(mpMemLayerSpec2, false);
			} else {
				uint8 base = 0x80;
				uint8 size = 0x20;
				uint32 extraOffset = 0;
				uint32 bank2 = mCartBank2;

				// adjust for 4K banking
				if (bank2 & 0x20000) {
					bank2 -= 0x20000;

					base = 0xA0;
					size = 0x10;

					if (bank2 & 1)
						extraOffset = 0x1000;

					bank2 += bank2 & 0x10000;
					bank2 >>= 1;
				}

				// if we are in a 16K mode, we must use the primary bank's read-only flag
				uint8 roBit = 0x04;

				switch(mTheCartBankMode) {
					default:
					case kTheCartBankMode_Disabled:
					case kTheCartBankMode_8K:
					case kTheCartBankMode_Flexi:
						// use secondary bank R/O bit (bit 2)
						break;

					case kTheCartBankMode_16K:
					case kTheCartBankMode_8KFixed_8K:
					case kTheCartBankMode_OSS:
					case kTheCartBankMode_SIC:
						// use primary bank R/O bit (bit 0)
						roBit = 0x01;
						break;
				}

				const bool readOnly = !(mTheCartRegs[7] & roBit);

				if (!(bank2 & 0x10000)) {
					// secondary bank set to flash
					mpMemMan->SetLayerMemory(mpMemLayerVarBank2, mpROM + (((bank2 << 13) + extraOffset) & mCartSizeMask), base, size, (uint32)0-1, true);
					mpMemMan->EnableLayer(mpMemLayerVarBank2, true);

					const bool flashRead = mFlashEmu.IsControlReadEnabled();
					mpMemMan->SetLayerAddressRange(mpMemLayerSpec2, base, size);
					mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
					mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
					mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPUWrite, !readOnly);
				} else {
					// secondary bank set to RAM
					mpMemMan->SetLayerMemory(mpMemLayerVarBank2, mCARTRAM.data() + (((bank2 << 13) + extraOffset) & 0x7ffff), base, size, (uint32)0-1, readOnly);
					mpMemMan->EnableLayer(mpMemLayerVarBank2, true);

					// disable flash layer
					mpMemMan->EnableLayer(mpMemLayerSpec2, false);
				}
			}
			return;
	}

	if (mCartBank2 < 0) {
		mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
		return;
	}

	mpMemMan->EnableLayer(mpMemLayerVarBank2, true);

	const uint8 *cartbase = mpROM;
	switch(mCartMode) {
		case kATCartridgeMode_BountyBob5200:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 12) + 0x4000);
			break;

		case kATCartridgeMode_BountyBob5200Alt:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 12) + 0x6000);
			break;

		case kATCartridgeMode_BountyBob800:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 12) + 0x4000);
			break;

		case kATCartridgeMode_OSS_034M:
		case kATCartridgeMode_OSS_043M:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + 0x3000);
			break;

		case kATCartridgeMode_OSS_M091:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase);
			break;

		case kATCartridgeMode_MDDOS:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 12));
			break;

		case kATCartridgeMode_AST_32K:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 8));
			break;
	}
}

void ATCartridgeEmulator::UpdateLayerBuses() {
	if (mpMemLayerFixedBank1) mpMemMan->SetLayerFastBus(mpMemLayerFixedBank1, mbFastBus);
	if (mpMemLayerFixedBank2) mpMemMan->SetLayerFastBus(mpMemLayerFixedBank2, mbFastBus);
	if (mpMemLayerVarBank1	) mpMemMan->SetLayerFastBus(mpMemLayerVarBank1, mbFastBus);
	if (mpMemLayerVarBank2	) mpMemMan->SetLayerFastBus(mpMemLayerVarBank2, mbFastBus);
	if (mpMemLayerSpec1		) mpMemMan->SetLayerFastBus(mpMemLayerSpec1, mbFastBus);
	if (mpMemLayerSpec2		) mpMemMan->SetLayerFastBus(mpMemLayerSpec2, mbFastBus);
	if (mpMemLayerControl	) mpMemMan->SetLayerFastBus(mpMemLayerControl, mbFastBus);
}

void ATCartridgeEmulator::UpdateLayerMasks() {
	uint32 maskStart;
	uint32 maskLen;

	if (mbRD4Gate) {
		if (mbRD5Gate) {
			maskStart = 0;
			maskLen = 0x100;
		} else {
			maskStart = 0x80;
			maskLen = 0x20;
		}
	} else {
		if (mbRD5Gate) {
			maskStart = 0xA0;
			maskLen = 0x20;
		} else {
			maskStart = 0;
			maskLen = 0;
		}
	}

	if (mpMemLayerFixedBank1) mpMemMan->SetLayerMaskRange(mpMemLayerFixedBank1, maskStart, maskLen);
	if (mpMemLayerFixedBank2) mpMemMan->SetLayerMaskRange(mpMemLayerFixedBank2, maskStart, maskLen);
	if (mpMemLayerVarBank1	) mpMemMan->SetLayerMaskRange(mpMemLayerVarBank1, maskStart, maskLen);
	if (mpMemLayerVarBank2	) mpMemMan->SetLayerMaskRange(mpMemLayerVarBank2, maskStart, maskLen);
	if (mpMemLayerSpec1		) mpMemMan->SetLayerMaskRange(mpMemLayerSpec1, maskStart, maskLen);
	if (mpMemLayerSpec2		) mpMemMan->SetLayerMaskRange(mpMemLayerSpec2, maskStart, maskLen);

	if (mpMemLayerControl	) mpMemMan->SetLayerMaskRange(mpMemLayerControl, 0, mbCCTLGate ? 0x100 : 0);
}

void ATCartridgeEmulator::UpdateTheCartBanking() {
	mTheCartBankInfo.clear();
	mTheCartBankInfo.resize(256, -1);

	// Supported modes:
	//
	// $00: off, cartridge disabled
	// $01: 8k banks at $A000
	// $02: AtariMax 1MBit / 128k
	// $03: Atarimax 8MBit / 1MB
	// $04: OSS M091
	// $08: SDX 64k cart, $D5Ex banking
	// $09: Diamond GOS 64k cart, $D5Dx banking
	// $0A: Express 64k cart, $D57x banking
	// $0C: Atrax 128k cart
	// $0D: Williams 64k cart
	// $20: flexi mode (separate 8k banks at $A000 and $8000)
	// $21: standard 16k cart at $8000-$BFFF
	// $22: MegaMax 16k mode (up to 2MB), AtariMax 8Mbit banking
	// $23: Blizzard 16k
	// $24: Sic!Cart 512k
	// $28: 16k Mega cart
	// $29: 32k Mega cart
	// $2A: 64k Mega cart
	// $2B: 128k Mega cart
	// $2C: 256k Mega cart
	// $2D: 512k Mega cart
	// $2E: 1024k Mega cart
	// $2F: 2048k Mega cart
	// $30: 32k XEGS cart
	// $31: 64k XEGS cart
	// $32: 128k XEGS cart
	// $33: 256k XEGS cart
	// $34: 512k XEGS cart
	// $35: 1024k XEGS cart
	// $38: 32k SWXEGS cart
	// $39: 64k SWXEGS cart
	// $3A: 128k SWXEGS cart
	// $3B: 256k SWXEGS cart
	// $3C: 512k SWXEGS cart
	// $3D: 1024k SWXEGS cart

	const uint8 mode = mTheCartRegs[6] & 0x3f;

	mTheCartBankMask = 0;
	mbTheCartBankByAddress = false;

	// validate mode and determine if we need the bank info
	switch(mode) {
		case 0x00:	// $00: off, cartridge disabled
			mTheCartBankMode = kTheCartBankMode_Disabled;
			break;

		case 0x01:	// $01: 8k banks at $A000
			mTheCartBankMode = kTheCartBankMode_8K;
			break;

		case 0x20:	// $20: flexi mode (separate 8k banks at $A000 and $8000)
			mTheCartBankMode = kTheCartBankMode_Flexi;
			break;

		case 0x21:	// $21: standard 16k cart at $8000-$BFFF
			mTheCartBankMode = kTheCartBankMode_16K;
			break;

		case 0x02:	// $02: AtariMax 1MBit / 128k
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x0F;

			for(int i=0; i<0x10; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;

			for(int i=0x10; i<0x20; ++i)
				mTheCartBankInfo[i] = i;
			break;

		case 0x03:	// $03: Atarimax 8MBit / 1MB
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x7F;

			for(int i=0; i<0x80; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;

			// The!Cart docs say that only $D58x disables in AtariMax 8Mbit emulation mode,
			// even though $D580-FF do so in the real cart.
			for(int i=0x80; i<0x90; ++i)
				mTheCartBankInfo[i] = i;
			break;

		case 0x04:	// $04: OSS M091
			{
				static const sint8 kBankLookup[16] = {1, 3, 1, 3, 1, 3, 1, 3, -1, 2};

				mTheCartBankMode = kTheCartBankMode_OSS;
				mbTheCartBankByAddress = true;
				mTheCartBankMask = 0x01;

				for(int i=0; i<256; ++i) {
					sint8 v = kBankLookup[i & 9];
					mTheCartBankInfo[i] = v;
				}
			}
			break;

		case 0x08:	// $08: SDX 64k cart, $D5Ex banking
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xE0+i] = 0x4007 - i;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xE8+i] = 7 - i;			
			break;

		case 0x09:	// $09: Diamond GOS 64k cart, $D5Dx banking
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xD0+i] = 0x4007 - i;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xD8+i] = 7 - i;			
			break;

		case 0x0A:	// $0A: Express 64k cart, $D57x banking
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0x70+i] = 0x4007 - i;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0x78+i] = 7 - i;			
			break;

		case 0x0C:	// $0C: Atrax 128k cart
			mTheCartBankMode = kTheCartBankMode_8K;
			mTheCartBankMask = 0x0F;
			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[0x80+i] = i;			
			break;

		case 0x0D:	// $0D: Williams 64k cart
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<0x100; ++i)
				mTheCartBankInfo[i] = i & 8 ? i : 0x4000 + i;
			break;

		case 0x22:	// $22: MegaMax 16k mode (up to 2MB), AtariMax 8Mbit banking
			mTheCartBankMode = kTheCartBankMode_16K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0xFE;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i] = 0x4000 + i*2;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i+128] = i*2;
			break;

		case 0x23:	// $23: Blizzard 16k
			mTheCartBankMode = kTheCartBankMode_16K;
			mTheCartBankMask = 0;
			for(int i=0; i<0x100; ++i)
				mTheCartBankInfo[i] = 0;			
			break;

		case 0x24:	// $24: Sic!Cart 512k
			mTheCartBankMode = kTheCartBankMode_SIC;
			mTheCartBankMask = 0x3E;

			for(int i=0; i<0x100; ++i) {
				// Bit 7 controls flash -- which we ignore here.
				// Bit 6 disables $A000-BFFF.
				// Bit 5 enables $8000-9FFF.
				// SIC! uses 16K banks, so we have to go evens.
				mTheCartBankInfo[i] = ((i & 0x40) ? 0 : 0x4000) + ((i & 0x20) ? 0x2000 : 0) + (i & 0x1f)*2;
			}
			break;

		case 0x25:	// $25: 4096k Mega Cart
			// 16K banks - data written to $D5xx selects bank, $FF disables
			mTheCartBankMode = kTheCartBankMode_16K;
			mTheCartBankMask = 0x1FE;
			mbTheCartBankByAddress = false;

			for(int i=0; i<255; ++i)
				mTheCartBankInfo[i] = 0x4000 + i * 2;

			mTheCartBankInfo[255] = 0;
			break;


		case 0x28:	// $28: 16k Mega cart
		case 0x29:	// $29: 32k Mega cart
		case 0x2A:	// $2A: 64k Mega cart
		case 0x2B:	// $2B: 128k Mega cart
		case 0x2C:	// $2C: 256k Mega cart
		case 0x2D:	// $2D: 512k Mega cart
		case 0x2E:	// $2E: 1024k Mega cart
		case 0x2F:	// $2F: 2048k Mega cart
			// 16K banks - data written to $D5xx selects bank, D7=1 disables
			mTheCartBankMode = kTheCartBankMode_16K;
			mTheCartBankMask = (2 << (mode - 0x28)) - 2;
			mbTheCartBankByAddress = false;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i] = 0x4000 + i * 2;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i + 128] = i * 2;
			break;

		case 0x30:	// $30: 32k XEGS cart
		case 0x31:	// $31: 64k XEGS cart
		case 0x32:	// $32: 128k XEGS cart
		case 0x33:	// $33: 256k XEGS cart
		case 0x34:	// $34: 512k XEGS cart
		case 0x35:	// $35: 1024k XEGS cart
			// fixed upper 8K bank, data written to $D5xx selects lower 8K bank
			mTheCartBankMode = kTheCartBankMode_8KFixed_8K;
			mTheCartBankMask = (4 << (mode - 0x30)) - 1;
			mbTheCartBankByAddress = false;

			for(int i=0; i<256; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;
			break;

		case 0x38:	// $38: 32k SWXEGS cart
		case 0x39:	// $39: 64k SWXEGS cart
		case 0x3A:	// $3A: 128k SWXEGS cart
		case 0x3B:	// $3B: 256k SWXEGS cart
		case 0x3C:	// $3C: 512k SWXEGS cart
		case 0x3D:	// $3D: 1024k SWXEGS cart
			// fixed upper 8K bank, data written to $D5xx selects lower 8K bank, D7=1 enables
			mTheCartBankMode = kTheCartBankMode_8KFixed_8K;
			mTheCartBankMask = (4 << (mode - 0x38)) - 1;
			mbTheCartBankByAddress = false;

			for(int i=0; i<256; ++i)
				mTheCartBankInfo[i] = ((i & 0x80) ? 0x4000 : 0) + i;
			break;
	}
}

void ATCartridgeEmulator::UpdateTheCart() {
	// compute primary bank
	sint32 bank1 = -1;

	if (mTheCartRegs[2] & 0x01) {		// check if enabled
		bank1 = VDReadUnalignedLEU16(mTheCartRegs + 0) & 0x3FFF;

		if (mTheCartRegs[7] & 0x02)		// check if RAM
			bank1 += 0x10000;
	}

	// compute secondary bank
	sint32 bank2 = -1;

	if (mTheCartRegs[5] & 0x01) {		// check if enabled
		bank2 = VDReadUnalignedLEU16(mTheCartRegs + 3) & 0x3FFF;

		if (mTheCartRegs[7] & 0x08)		// check if RAM
			bank2 += 0x10000;
	}

	switch(mTheCartBankMode) {
		case kTheCartBankMode_Disabled:
		default:
			SetCartBank(-1);
			SetCartBank2(-1);
			break;

		case kTheCartBankMode_8K:
			SetCartBank(bank1);
			SetCartBank2(-1);
			break;

		case kTheCartBankMode_16K:
			bank1 &= ~1;
			SetCartBank(bank1 + 1);
			SetCartBank2(bank1);
			break;

		case kTheCartBankMode_SIC:
			if (mTheCartRegs[2]) {
				// recompute primary bank since there are independent enables (arg)
				bank1 = VDReadUnalignedLEU16(mTheCartRegs + 0) & 0x3FFF;

				if (mTheCartRegs[7] & 0x02)		// check if RAM
					bank1 += 0x10000;

				bank1 &= ~1;
				SetCartBank(mTheCartSICEnables & 0x40 ? bank1+1 : -1);
				SetCartBank2(mTheCartSICEnables & 0x20 ? bank1 : -1);
			} else {
				SetCartBank(-1);
				SetCartBank2(-1);
			}
			break;

		case kTheCartBankMode_Flexi:
			SetCartBank(bank1);
			SetCartBank2(bank2);
			break;

		case kTheCartBankMode_8KFixed_8K:
			SetCartBank(bank1 | mTheCartBankMask);
			SetCartBank2(bank1);
			break;

		case kTheCartBankMode_OSS:
			SetCartBank(bank1 | 0x20000);
			SetCartBank2(((bank1 & ~1) * 2) | 0x20000 | mTheCartOSSBank);
			break;
	}
}

void ATCartridgeEmulator::InitDebugBankMap() {
	ResetDebugBankMap();

	switch(mCartMode) {
		case kATCartridgeMode_2K:
		case kATCartridgeMode_4K:
		case kATCartridgeMode_8K:
			for(auto& bank : mDebugBankMap) {
				bank[2] = 0;
				bank[3] = 0x1000;
			}
			break;

		case kATCartridgeMode_16K:
			for(auto& bank : mDebugBankMap) {
				bank[0] = 0;
				bank[1] = 0x1000;
				bank[2] = 0x2000;
				bank[3] = 0x3000;
			}
			break;

		case kATCartridgeMode_SpartaDosX_64K:
		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_SpartaDosX_128K:
		case kATCartridgeMode_MaxFlash_1024K:
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
		case kATCartridgeMode_JAtariCart_8K:
		case kATCartridgeMode_JAtariCart_16K:
		case kATCartridgeMode_JAtariCart_32K:
		case kATCartridgeMode_JAtariCart_64K:
		case kATCartridgeMode_JAtariCart_128K:
		case kATCartridgeMode_JAtariCart_256K:
		case kATCartridgeMode_JAtariCart_512K:
		case kATCartridgeMode_JAtariCart_1024K:
		case kATCartridgeMode_DCart:
			for(uint32 i=0, n = mCartSize >> 13; i < n; ++i) {
				auto& bank = mDebugBankMap[i];

				const sint32 baseOffset = i << 13;
				bank[2] = baseOffset;
				bank[3] = baseOffset + 0x1000;
			}
			break;

		case kATCartridgeMode_JRC6_64K:
		case kATCartridgeMode_JRC_RAMBOX:
			for(uint32 i=0; i<8; ++i) {
				auto& bank = mDebugBankMap[i];

				const sint32 baseOffset = i << 13;
				bank[2] = baseOffset;
				bank[3] = baseOffset + 0x1000;
			}
			break;
	}
}

void ATCartridgeEmulator::ResetDebugBankMap() {
	for(auto& bank : mDebugBankMap) {
		for(auto& block : bank)
			block = -1;
	}
}
