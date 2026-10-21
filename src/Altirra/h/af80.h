// AltirraSDL Austin Franklin 80 column board. GPL-2.0-or-later.
#ifndef f_AT_AF80_H
#define f_AT_AF80_H

#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicecart.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/devicevideo.h>
#include <vd2/Kasumi/pixmaputils.h>

class ATMemoryManager;
class ATMemoryLayer;

class ATDeviceAF80 final : public ATDeviceT<IATDeviceMemMap,
	IATDeviceFirmware, IATDeviceCartridge, IATDeviceVideoOutput,
	IATDeviceSnapshot> {
public:
	~ATDeviceAF80();
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32) const override { return nullptr; }
	bool IsWritableFirmwareDirty(uint32) const override { return false; }
	void SaveWritableFirmware(uint32, IVDStream&) override {}
	ATDeviceFirmwareStatus GetFirmwareStatus() const override { return mFWStatus; }
	void InitCartridge(IATDeviceCartridgePort *port) override;
	bool IsLeftCartActive() const override { return false; }
	void SetCartEnables(bool left, bool right, bool cctl) override;
	void UpdateCartSense(bool) override {}
	const char *GetName() const override { return "af80"; }
	const wchar_t *GetDisplayName() const override { return L"Austin Franklin 80"; }
	void Tick(uint32 ticks) override;
	void UpdateFrame() override;
	const VDPixmap& GetFrameBuffer() override { return mFrame; }
	const ATDeviceVideoInfo& GetVideoInfo() override;
	vdpoint32 PixelToCaretPos(const vdpoint32& pos) override;
	vdrect32 CharToPixelRect(const vdrect32& r) override;
	int ReadRawText(uint8 *dst, int x, int y, int n) override;
	uint32 GetActivityCounter() override { return mActivity; }
	void LoadState(const IATObjectState *state, ATSnapshotContext&) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext&) const override;

private:
	sint32 OnRead(uint32 addr);
	bool OnWrite(uint32 addr, uint8 value);
	void UpdateMapping();
	unsigned GetTextAddress(unsigned row, unsigned col) const;
	uint8 GetPixels(unsigned row, unsigned col, unsigned line) const;
	ATMemoryManager *mpMemMgr = nullptr;
	ATMemoryLayer *mpIO = nullptr;
	ATMemoryLayer *mpCart = nullptr;
	ATFirmwareManager *mpFwMgr = nullptr;
	IATDeviceCartridgePort *mpCartPort = nullptr;
	IATDeviceVideoManager *mpVideoMgr = nullptr;
	uint32 mCartId = 0;
	bool mbRightEnabled = true;
	bool mbCCTLEnabled = true;
	ATDeviceFirmwareStatus mFWStatus = ATDeviceFirmwareStatus::Missing;
	uint8 mROM[4096] {};
	uint8 mCharset[4096] {};
	uint8 mScreen[2048] {};
	uint8 mAttributes[2048] {};
	uint8 mRegisters[64] {};
	uint8 mControl = 0;
	uint8 mROMControl = 0;
	uint32 mTicks = 0;
	uint32 mActivity = 0;
	bool mbDirty = true;
	ATDeviceVideoInfo mInfo {};
	VDPixmapBuffer mFrame;
};

#endif
