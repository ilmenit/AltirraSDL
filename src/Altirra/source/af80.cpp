// AltirraSDL Austin Franklin 80 column board. GPL-2.0-or-later.
// Hardware behavior adapted from Atari800 src/af80.c:
// Copyright (C) 2009 Perry McFarlane and the Atari800 development team.
// Its register map is based on warerat's AtariAge hardware investigation.
// Preserve the original 80x25, 8x10 character cell, attribute priority,
// split-screen address registers and palette wiring rather than assuming
// this board uses the incompatible Bit 3/6845 register interface.
#include <stdafx.h>
#include <algorithm>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include "af80.h"
#include "firmwaremanager.h"
#include "memorymanager.h"

void ATCreateDeviceAF80(const ATPropertySet&, IATDevice **dev) {
	*dev = new ATDeviceAF80;
	(*dev)->AddRef();
}
extern const ATDeviceDefinition g_ATDeviceDefAF80 {
	"af80", nullptr, L"Austin Franklin 80 column board", ATCreateDeviceAF80
};

struct ATSaveStateAF80 final
	: ATSnapExchangeObject<ATSaveStateAF80, "ATSaveStateAF80"> {
	uint8 mControl = 0;
	uint8 mROMControl = 0;
	uint32 mTicks = 0;
	vdrefptr<ATSaveStateMemoryBuffer> mpMemory;
	template<ATExchanger T> void Exchange(T& ex) {
		ex.Transfer("control", &mControl);
		ex.Transfer("rom_control", &mROMControl);
		ex.Transfer("ticks", &mTicks);
		ex.Transfer("memory", &mpMemory);
	}
};

ATDeviceAF80::~ATDeviceAF80() { Shutdown(); }
void ATDeviceAF80::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefAF80;
}
void ATDeviceAF80::InitMemMap(ATMemoryManager *memmap) {
	mpMemMgr = memmap;
}
void ATDeviceAF80::InitCartridge(IATDeviceCartridgePort *port) {
	mpCartPort = port;
	port->AddCartridge(this, kATCartridgePriority_Default, mCartId);
}
void ATDeviceAF80::SetCartEnables(bool, bool right, bool cctl) {
	mbRightEnabled = right;
	mbCCTLEnabled = cctl;
	UpdateMapping();
}
void ATDeviceAF80::Init() {
	ATMemoryHandlerTable ht {};
	ht.mpThis = this;
	ht.mbPassAnticReads = false;
	ht.mbPassReads = true;
	ht.mbPassWrites = true;
	ht.BindDebugReadHandler<&ATDeviceAF80::OnRead>();
	ht.BindReadHandler<&ATDeviceAF80::OnRead>();
	ht.BindWriteHandler<&ATDeviceAF80::OnWrite>();
	mpIO = mpMemMgr->CreateLayer(kATMemoryPri_PBI, ht, 0xD5, 2);
	mpMemMgr->SetLayerName(mpIO, "Austin Franklin registers and ROM");
	mpMemMgr->EnableLayer(mpIO, true);
	// The right cartridge repeats the selected 256-byte ROM bank.
	mpCart = mpMemMgr->CreateLayer(kATMemoryPri_Cartridge1,
		ht, 0x80, 0x20);
	mpMemMgr->SetLayerName(mpCart, "Austin Franklin right cartridge");
	mFrame.init(640, 250, nsVDPixmap::kPixFormat_XRGB8888);
	mInfo.mbSignalValid = true;
	mInfo.mHorizScanRate = 15734.0f;
	mInfo.mVertScanRate = 60.0f;
	mInfo.mFrameBufferLayoutChangeCount = 1;
	mInfo.mTextRows = 25;
	mInfo.mTextColumns = 80;
	mInfo.mPixelAspectRatio = 0.5;
	mInfo.mDisplayArea = vdrect32(0, 0, 640, 250);
	ReloadFirmware();
	ColdReset();
	mpVideoMgr = GetService<IATDeviceVideoManager>();
	if (mpVideoMgr)
		mpVideoMgr->AddVideoOutput(this);
}
void ATDeviceAF80::Shutdown() {
	if (mpVideoMgr) {
		mpVideoMgr->RemoveVideoOutput(this);
		mpVideoMgr = nullptr;
	}
	if (mpCartPort) {
		mpCartPort->RemoveCartridge(mCartId, this);
		mpCartPort = nullptr;
	}
	if (mpMemMgr) {
		mpMemMgr->DeleteLayerPtr(&mpIO);
		mpMemMgr->DeleteLayerPtr(&mpCart);
		mpMemMgr = nullptr;
	}
	mpFwMgr = nullptr;
}
void ATDeviceAF80::ColdReset() {
	memset(mScreen, 0, sizeof mScreen);
	memset(mAttributes, 0, sizeof mAttributes);
	memset(mRegisters, 0, sizeof mRegisters);
	mControl = 0;
	mROMControl = 0;
	mTicks = 0;
	mbDirty = true;
	UpdateMapping();
}
bool ATDeviceAF80::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index >= 2)
		return false;
	lo = index ? 0xD500 : 0x8000;
	hi = index ? 0xD6FF : 0x9FFF;
	return true;
}
void ATDeviceAF80::InitFirmware(ATFirmwareManager *mgr) { mpFwMgr = mgr; }
bool ATDeviceAF80::ReloadFirmware() {
	mFWStatus = ATDeviceFirmwareStatus::OK;
	bool changed = false;
	const uint8 fill = 0xFF;
	const ATFirmwareType types[] = {
		kATFirmwareType_AF80Firmware, kATFirmwareType_AF80Charset
	};
	uint8 *buffers[] = { mROM, mCharset };
	for (int i = 0; i < 2; ++i) {
		bool usable = false;
		bool thisChanged = false;
		uint32 actualLen = 0;
		if (!mpFwMgr || !mpFwMgr->LoadFirmware(
			mpFwMgr->GetCompatibleFirmware(types[i]), buffers[i], 0, 4096,
			&thisChanged, &actualLen, nullptr, &fill, &usable)) {
			if (mFWStatus != ATDeviceFirmwareStatus::Invalid)
				mFWStatus = ATDeviceFirmwareStatus::Missing;
		} else if (!usable || actualLen != 4096)
			mFWStatus = ATDeviceFirmwareStatus::Invalid;
		changed |= thisChanged;
	}
	mbDirty = true;
	return changed;
}
void ATDeviceAF80::UpdateMapping() {
	if (mpCart)
		mpMemMgr->EnableLayer(mpCart, mbRightEnabled && !(mROMControl & 0x20));
}
sint32 ATDeviceAF80::OnRead(uint32 addr) {
	if (addr < 0xA000)
		return mROMControl & 0x10 ? 0xFF
			: mROM[((mROMControl & 15) << 8) + (addr & 255)];
	if (addr < 0xD600) {
		// The cartridge PROM leaves F0-F7 undriven for the board's
		// write-only latches. ROM output-disable also releases CCTL.
		// Hardware reverse engineering, warerat 2009-12-07:
		// forums.atariage.com/topic/154535-austin-franklin-80-information/
		if (!mbCCTLEnabled || (mROMControl & 0x10)
			|| (addr >= 0xD5F0 && addr <= 0xD5F7))
			return -1;
		return mROM[((mROMControl & 15) << 8) + (addr & 255)];
	}
	unsigned offset = ((mControl & 15) << 7) + (addr & 127);
	if (!(mControl & 0x10))
		return mScreen[offset];
	if (!(mControl & 0x20))
		return mAttributes[offset];
	if (!(mControl & 0x40) && !(mControl & 15) && (addr & 127) < 64)
		return (addr & 127) == 0x3A ? 1 : mRegisters[addr & 63];
	return 0xFF;
}
bool ATDeviceAF80::OnWrite(uint32 addr, uint8 value) {
	if (addr < 0xA000)
		return true;
	if (addr < 0xD600) {
		if (!mbCCTLEnabled)
			return false;
		if (addr == 0xD5F6)
			mControl = value;
		else if (addr == 0xD5F7) {
			mROMControl = value;
			UpdateMapping();
		} else
			return false;
	} else {
		unsigned offset = ((mControl & 15) << 7) + (addr & 127);
		if (!(mControl & 0x10))
			mScreen[offset] = value;
		else if (!(mControl & 0x20))
			mAttributes[offset] = value;
		else if (!(mControl & 0x40) && !(mControl & 15) && (addr & 127) < 64)
			mRegisters[addr & 63] = value;
	}
	++mActivity;
	mbDirty = true;
	return true;
}
unsigned ATDeviceAF80::GetTextAddress(unsigned row, unsigned col) const {
	unsigned address;
	if (row >= mRegisters[0x10])
		address = (row - mRegisters[0x10]) * 80 + col
			+ mRegisters[0x0E] + ((mRegisters[0x0F] & 63) << 8);
	else
		address = row * 80 + col + mRegisters[0x0C]
			+ ((mRegisters[0x0D] & 63) << 8);
	return address & 2047;
}
uint8 ATDeviceAF80::GetPixels(unsigned row, unsigned col, unsigned line) const {
	const unsigned address = GetTextAddress(row, col);
	const uint8 attr = mAttributes[address];
	uint8 bits = mCharset[mScreen[address] * 16 + line];
	if (attr & 1)
		bits ^= 255;
	const bool blink = mTicks >= 150;
	if ((attr & 2) && blink)
		bits = 0;
	if (line == 9 && (attr & 4))
		bits = 255;
	if (row == mRegisters[0x18] && col == mRegisters[0x19] && !blink)
		bits = 255;
	return bits;
}
void ATDeviceAF80::Tick(uint32 ticks) {
	const bool oldBlink = mTicks >= 150;
	mTicks = (mTicks + ticks % 300) % 300;
	mbDirty |= oldBlink != (mTicks >= 150);
}
const ATDeviceVideoInfo& ATDeviceAF80::GetVideoInfo() {
	// D5F6 bit 7 selects 80-column output when set. The original
	// cartridge uses B0/E0/D0 for CRTC/character/attribute accesses while
	// active, and 70 at startup. Atari800 stores this bit but never applies
	// it to rendering; its inverted variable name is not a wiring reference.
	mInfo.mbSignalPassThrough = (mControl & 0x80) == 0;
	return mInfo;
}
void ATDeviceAF80::UpdateFrame() {
	if (!mbDirty)
		return;
	// AF80 attribute wiring is RGBI with red/blue exchanged versus CGA.
	constexpr uint32 colors[] = {
		0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
		0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
		0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
		0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
	};
	for (unsigned y = 0; y < 250; ++y) {
		auto *dst = (uint32 *)((char *)mFrame.data + y * mFrame.pitch);
		for (unsigned x = 0; x < 80; ++x) {
			const uint8 bits = (mControl & 0x80) ? GetPixels(y / 10, x, y % 10) : 0;
			const uint32 color = colors[mAttributes[GetTextAddress(y / 10, x)] >> 4];
			// AF80 charset bytes put the leftmost dot in bit 0, as in
			// Atari800's src/sdl/video.c AF80 renderer. MSB-first mirrors
			// every glyph in the original board's character ROM.
			for (unsigned b = 0; b < 8; ++b)
				*dst++ = (bits & (1U << b)) ? color : 0;
		}
	}
	++mInfo.mFrameBufferChangeCount;
	mbDirty = false;
}
vdpoint32 ATDeviceAF80::PixelToCaretPos(const vdpoint32& pos) {
	if (pos.y < 0) return vdpoint32(0, 0);
	if (pos.y >= 250) return vdpoint32(80, 24);
	return vdpoint32((std::clamp(pos.x, -4, 640) + 4) / 8, pos.y / 10);
}
vdrect32 ATDeviceAF80::CharToPixelRect(const vdrect32& r) {
	return vdrect32(r.left * 8, r.top * 10, r.right * 8, r.bottom * 10);
}
int ATDeviceAF80::ReadRawText(uint8 *dst, int x, int y, int n) {
	if (x < 0 || x >= 80 || y < 0 || y >= 25 || n <= 0)
		return 0;
	n = std::min(n, 80 - x);
	for (int i = 0; i < n; ++i)
		dst[i] = mScreen[GetTextAddress(y, x + i)];
	return n;
}
vdrefptr<IATObjectState> ATDeviceAF80::SaveState(ATSnapshotContext&) const {
	vdrefptr state { new ATSaveStateAF80 };
	state->mControl = mControl;
	state->mROMControl = mROMControl;
	state->mTicks = mTicks;
	state->mpMemory = new ATSaveStateMemoryBuffer;
	state->mpMemory->mpDirectName = L"af80-ram.bin";
	auto& b = state->mpMemory->GetWriteBuffer();
	b.assign(mScreen, mScreen + 2048);
	b.insert(b.end(), mAttributes, mAttributes + 2048);
	b.insert(b.end(), mRegisters, mRegisters + 64);
	return state;
}
void ATDeviceAF80::LoadState(const IATObjectState *state, ATSnapshotContext&) {
	if (!state) { ColdReset(); return; }
	const auto& s = atser_cast<const ATSaveStateAF80&>(*state);
	if (!s.mpMemory || s.mpMemory->GetReadBuffer().size() != 4160
		|| s.mTicks >= 300)
		throw ATInvalidSaveStateException();
	const uint8 *p = s.mpMemory->GetReadBuffer().data();
	memcpy(mScreen, p, 2048);
	memcpy(mAttributes, p + 2048, 2048);
	memcpy(mRegisters, p + 4096, 64);
	mControl = s.mControl;
	mROMControl = s.mROMControl;
	mTicks = s.mTicks;
	mbDirty = true;
	++mActivity;
	UpdateMapping();
}
