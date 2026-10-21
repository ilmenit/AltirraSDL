// AltirraSDL Mosaic 64K Select expansion. GPL-2.0-or-later.
#ifndef f_AT_MOSAIC_H
#define f_AT_MOSAIC_H

#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesnapshot.h>
#include <vd2/system/vdstl.h>

class ATMemoryLayer;
class ATMemoryManager;

class ATDeviceMosaic final
	: public ATDeviceT<IATDeviceMemMap, IATDeviceSnapshot> {
public:
	~ATDeviceMosaic();
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

private:
	bool OnWrite(uint32 addr, uint8 value);
	void UpdateMapping();
	ATMemoryManager *mpMemMgr = nullptr;
	ATMemoryLayer *mpRAM = nullptr;
	ATMemoryLayer *mpControl = nullptr;
	vdfastvector<uint8> mMemory;
	uint32 mBanks = 4;
	uint8 mBank = 0;
};

#endif
