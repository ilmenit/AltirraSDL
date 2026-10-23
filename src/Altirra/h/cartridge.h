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

#ifndef f_AT_CARTRIDGE_H
#define f_AT_CARTRIDGE_H

#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <at/atcore/checksum.h>
#include <at/atcore/devicecart.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/scheduler.h>
#include <at/atio/cartridgeimage.h>
#include <at/atemulation/flash.h>
#include <at/atemulation/eeprom.h>

class ATSaveStateReader;
class IVDRandomAccessStream;
class IATDeviceIndicatorManager;
class ATMemoryManager;
class ATMemoryLayer;
class ATScheduler;
class IATObjectState;
struct ATCartridgeModeTable;

// Per-mapper physical defaults; the SDL frontend persists these per profile.
uint32 ATGetRamCartDefaultSwitches(ATCartridgeMode mode);
void ATSetRamCartDefaultSwitches(ATCartridgeMode mode, uint32 switches);

class ATCartridgeEmulator final
	: public ATDeviceT<IATDeviceCartridge, IATDeviceButtons>
	, public IATSchedulerCallback
{
	friend struct ATCartridgeModeTable;

public:
	ATCartridgeEmulator(int basePriority, ATCartridgePriority cartPri, bool fastBus);
	~ATCartridgeEmulator();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;

	void SetFastBus(bool fastBus);

	int GetCartBank() const { return mCartBank; }
	bool IsBASICDisableAllowed() const;		// Cleared if we have a cart type that doesn't want OPTION pressed (AtariMax).
	bool IsDirty() const { return mbDirty; }
	
	const wchar_t *GetPath() const;

	ATCartridgeMode GetMode() const { return mCartMode; }
	IATCartridgeImage *GetImage() const { return mpImage; }

	std::optional<uint32> GetImageFileCRC() const;
	std::optional<ATChecksumSHA256> GetImageFileSHA256() const;

	bool IsDefaultCartridge() const { return mbIsDefaultCart; }

	void Load5200Default();
	void LoadNewCartridge(ATCartridgeMode mode);
	bool Load(const wchar_t *fn, ATCartLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, IVDRandomAccessStream& stream, ATCartLoadContext *loadCtx);
	void Load(IATCartridgeImage *image);
	void Unload();

	void Save(const wchar_t *fn, bool includeHeader);

	// RAM-Cart physical controls. Bit encoding follows Atari800's cartridge
	// state: R/W=12, P1=13, P2=14, ABC jumpers=15, decoder=16,
	// readback=17; A/B/C=2/6/7 and upper modules=8/9.
	uint32 GetRamCartSwitchMask() const;
	uint32 GetRamCartSwitches() const;
	void SetRamCartSwitches(uint32 switches);

	void ColdReset() override;

	void BeginLoadState(ATSaveStateReader& reader);
	void LoadStatePrivate(ATSaveStateReader& reader);
	void EndLoadState(ATSaveStateReader& reader);

	void LoadState(IATObjectState& state);
	void SaveState(IATObjectState **pp);

	uint8 DebugReadLinear(uint32 offset) const;
	uint8 DebugReadBanked(uint32 globalAddress) const;

public:		// IATDeviceCartridge
	void InitCartridge(IATDeviceCartridgePort *cartPort) override;
	bool IsLeftCartActive() const override;
	void SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) override;
	void UpdateCartSense(bool leftActive) override;

public:		// IATDeviceButtons
	uint32 GetSupportedButtons() const override;
	bool IsButtonDepressed(ATDeviceButton idx) const override;
	void ActivateButton(ATDeviceButton idx, bool state) override;
	
public:		// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

protected:
	struct LayerEnables {
		bool mbRead;
		bool mbWrite;
		bool mbRD5Masked;

		void Init(uint8 pageBase, uint8 pageSize);
	};

	template<class T> void ExchangeState(T& io);

	static sint32 ReadByte_Unmapped(void *thisptr0, uint32 address);
	static bool WriteByte_Unmapped(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_BB5200_1(void *thisptr0, uint32 address);
	static sint32 ReadByte_BB5200_2(void *thisptr0, uint32 address);
	static bool WriteByte_BB5200_1(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_BB5200_2(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_BB800_1(void *thisptr0, uint32 address);
	static sint32 ReadByte_BB800_2(void *thisptr0, uint32 address);
	static bool WriteByte_BB800_1(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_BB800_2(void *thisptr0, uint32 address, uint8 value);

	static sint32 DebugReadByte_SIC(void *thisptr0, uint32 address);
	static sint32 ReadByte_SIC(void *thisptr0, uint32 address);
	static bool WriteByte_SIC(void *thisptr0, uint32 address, uint8 value);

	static sint32 DebugReadByte_SICPlus(void *thisptr0, uint32 address);
	static sint32 ReadByte_SICPlus(void *thisptr0, uint32 address);
	static bool WriteByte_SICPlus(void *thisptr0, uint32 address, uint8 value);

	static sint32 DebugReadByte_TheCart(void *thisptr0, uint32 address);
	static sint32 ReadByte_TheCart(void *thisptr0, uint32 address);
	static bool WriteByte_TheCart(void *thisptr0, uint32 address, uint8 value);
	static sint32 DebugReadByte_MegaCart3(void *thisptr0, uint32 address);
	static sint32 ReadByte_MegaCart3(void *thisptr0, uint32 address);
	static bool WriteByte_MegaCart3(void *thisptr0, uint32 address, uint8 value);

	static sint32 DebugReadByte_MaxFlash(void *thisptr0, uint32 address);
	static sint32 ReadByte_MaxFlash(void *thisptr0, uint32 address);
	static bool WriteByte_MaxFlash(void *thisptr0, uint32 address, uint8 value);

	static sint32 DebugReadByte_JAtariCart_512K(void *thisptr0, uint32 address);
	static sint32 ReadByte_JAtariCart_512K(void *thisptr0, uint32 address);
	static bool WriteByte_JAtariCart_512K(void *thisptr0, uint32 address, uint8 value);

	static bool WriteByte_Corina1M(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_Corina512K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_TelelinkII(void *thisptr0, uint32 address);
	static bool WriteByte_TelelinkII(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_Pronto(void *thisptr0, uint32 address);
	static bool WriteByte_Pronto(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_Phoenix(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Phoenix(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_Blizzard_32K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Blizzard_32K(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_AddressToBank(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static sint32 ReadByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_DataToBank(void *thisptr0, uint32 address, uint8 value);

	static bool WriteByte_CCTL_XEGS_64K_Alt(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_DataToBank_Switchable(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static sint32 ReadByte_CCTL_Williams(void *thisptr0, uint32 address);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_Williams(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Address, bool T_PassThrough>
	static sint32 DebugReadByte_CCTL_SDX64(void *thisptr0, uint32 address);

	template<uint8 T_Address, bool T_PassThrough>
	static sint32 ReadByte_CCTL_SDX64(void *thisptr0, uint32 address);

	template<uint8 T_Address, bool T_PassThrough>
	static bool WriteByte_CCTL_SDX64(void *thisptr0, uint32 address, uint8 value);

	template<bool T_PassThrough>
	static bool WriteByte_CCTL_SDX128(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_SIC(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_SIC(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_SICPlus(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_SICPlus(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_TheCart(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_TheCart(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_SC3D(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_SC3D(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_TelelinkII(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_TelelinkII(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_034M(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_034M(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_043M(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_043M(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_M091(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_M091(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_8K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_8K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MDDOS(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MDDOS(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_COS32K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_COS32K(void *thisptr0, uint32 address, uint8 value);
	
	static bool WriteByte_CCTL_Corina(void *thisptr0, uint32 address, uint8 value);
	
	static bool WriteByte_CCTL_AST_32K(void *thisptr0, uint32 address, uint8 value);
	
	static sint32 ReadByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_5200_512K_32KBanks(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_5200_512K_32KBanks(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MicroCalc(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MicroCalc(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MegaCart3(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MegaCart3_512K(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_CCTL_MegaCart3_4M(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_aDawliah_32K(void *thisptr0, uint32 address);
	static sint32 ReadByte_CCTL_aDawliah_64K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_aDawliah_32K(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_CCTL_aDawliah_64K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_RamCart(void *thisptr, uint32 address);
	static bool WriteByte_RamCart(void *thisptr, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_RamCart(void *thisptr, uint32 address);
	static bool WriteByte_CCTL_RamCart(void *thisptr, uint32 address, uint8 value);
	sint32 GetRamCartOffset(uint32 address) const;

	static sint32 ReadByte_CCTL_JRC_RAMBOX(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_JRC_RAMBOX(void *thisptr0, uint32 address, uint8 value);

	static bool WriteByte_CCTL_JRC6_64K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_XEMulticart(void *thisptr0, uint32 address);

	static sint32 ReadByte_CCTL_Pronto(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Pronto(void *thisptr0, uint32 address, uint8 value);
	
	static sint32 DebugReadByte_CCTL_DCart(void *thisptr0, uint32 address);
	static sint32 ReadByte_CCTL_DCart(void *thisptr0, uint32 address);

	void InitFromImage();
	void InitMemoryLayers();
	void ShutdownMemoryLayers();
	void SetCartBank(int bank);
	void SetCartBank2(int bank);
	void UpdateCartBank();
	void UpdateCartBank2();
	void UpdateLayerBuses();
	void UpdateLayerMasks();
	void UpdateTheCartBanking();
	void UpdateTheCart();

	void InitDebugBankMap();
	void ResetDebugBankMap();

	ATCartridgeMode mCartMode = kATCartridgeMode_None;
	int	mCartBank = -1;
	int	mCartBank2 = -1;
	uint32 mCartSizeMask = 0;
	int	mInitialCartBank = -1;
	int	mInitialCartBank2 = -1;
	int mBasePriority = 0;
	ATCartridgePriority mCartPriority {};
	bool mbDirty = false;
	bool mbRD4Gate = true;
	bool mbRD5Gate = true;
	bool mbCCTLGate = true;
	bool mbFastBus = false;
	bool mbCartEnabled = true;
	IATDeviceIndicatorManager *mpUIRenderer = nullptr;
	ATMemoryManager *mpMemMan = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventReset = nullptr;
	IATDeviceCartridgePort *mpCartridgePort = nullptr;
	uint32 mCartId = 0;

	LayerEnables mLayerEnablesFixedBank1;
	LayerEnables mLayerEnablesFixedBank2;
	LayerEnables mLayerEnablesVarBank1;
	LayerEnables mLayerEnablesVarBank2;
	LayerEnables mLayerEnablesSpec1;
	LayerEnables mLayerEnablesSpec2;

	ATMemoryLayer *mpMemLayerFixedBank1 = nullptr;
	ATMemoryLayer *mpMemLayerFixedBank2 = nullptr;
	ATMemoryLayer *mpMemLayerVarBank1 = nullptr;
	ATMemoryLayer *mpMemLayerVarBank2 = nullptr;
	ATMemoryLayer *mpMemLayerSpec1 = nullptr;
	ATMemoryLayer *mpMemLayerSpec2 = nullptr;
	ATMemoryLayer *mpMemLayerControl = nullptr;

	ATFlashEmulator mFlashEmu;
	ATFlashEmulator mFlashEmu2;
	ATEEPROMEmulator mEEPROM;

	union {
		uint8	mSC3D[4];
		uint8	mTheCartRegs[9];
	};

	uint8 *mpROM = nullptr;
	uint32	mCartSize = 0;
	bool mbIsDefaultCart = false;

	vdfastvector<uint8> mCARTRAM;

	vdrefptr<IATCartridgeImage> mpImage;

	vdblock<sint16> mTheCartBankInfo;
	bool mbTheCartBankByAddress;
	uint8 mTheCartOSSBank;
	uint8 mTheCartSICEnables;
	uint16 mTheCartBankMask;
	bool mbTheCartConfigLock;

	enum TheCartBankMode {
		kTheCartBankMode_Disabled,
		kTheCartBankMode_8K,
		kTheCartBankMode_16K,
		kTheCartBankMode_Flexi,
		kTheCartBankMode_8KFixed_8K,
		kTheCartBankMode_OSS,		// 4K fixed + 4K variable
		kTheCartBankMode_SIC		// 16K, independently switchable halves
	};

	TheCartBankMode mTheCartBankMode;

	// Map from bank and A10-A13 to raw cartridge bank offset.
	sint32 mDebugBankMap[256][4];
};

bool ATIsCartridgeModeHWCompatible(ATCartridgeMode cartmode, int hwmode);

#endif	// f_AT_CARTRIDGE_H
