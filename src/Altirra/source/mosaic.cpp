// AltirraSDL Mosaic 64K Select expansion. GPL-2.0-or-later.
#include <stdafx.h>
#include <algorithm>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include "memorymanager.h"
#include "mosaic.h"

// Hardware references: Mosaic 64K Select owner's manual, pp. 12-16,
// 31-33 (archive.org/details/mosaic-64-k-select-ram-owners-manual-with-updates-and-insert).
// The address, not the data, selects a 4K bank at C000-CFFF. Reads of
// the bank-selection addresses still read the OS ROM. The manual describes
// 52K of contiguous memory at power-on, so bank 0 starts enabled (unlike
// Atari800's explicitly uncertain disabled-at-power-on approximation).
// Decode of the entire FF page follows Michael Current's Atari 8-bit FAQ,
// Mosaic bank-selection type: uninstalled bank addresses deselect RAM.
// This device models the 800-strapped bank-select mode; the separate 1200
// OS-under-RAM modification is not part of the bank-select interface.

void ATCreateDeviceMosaic(const ATPropertySet& settings, IATDevice **dev) {
	vdrefptr p { new ATDeviceMosaic };
	p->SetSettings(settings);
	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMosaic {
	"mosaic", "mosaic", L"Mosaic 64K Select", ATCreateDeviceMosaic
};

struct ATSaveStateMosaic final
	: ATSnapExchangeObject<ATSaveStateMosaic, "ATSaveStateMosaic"> {
	uint8 mBank = 0;
	vdrefptr<ATSaveStateMemoryBuffer> mpMemory;
	template<ATExchanger T> void Exchange(T& ex) {
		ex.Transfer("bank", &mBank);
		ex.Transfer("memory", &mpMemory);
	}
};

ATDeviceMosaic::~ATDeviceMosaic() { Shutdown(); }
void ATDeviceMosaic::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefMosaic;
}
void ATDeviceMosaic::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("banks", mBanks);
}
bool ATDeviceMosaic::SetSettings(const ATPropertySet& settings) {
	const uint32 banks = settings.GetUint32("banks", 4);
	// Four banks per 16K of extended RAM, up to three 64K boards.
	const uint32 valid = banks >= 4 && banks <= 36 && !(banks & 3)
		? banks : 4;
	if (mpRAM && valid != mBanks)
		return false;
	mBanks = valid;
	return true;
}
void ATDeviceMosaic::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"%uK total, %u banks", 48 + mBanks * 4, mBanks);
}
void ATDeviceMosaic::InitMemMap(ATMemoryManager *memmap) {
	mpMemMgr = memmap;
}
void ATDeviceMosaic::Init() {
	mMemory.resize(mBanks * 4096);
	mpRAM = mpMemMgr->CreateLayer(kATMemoryPri_ExtRAMExtsel,
		mMemory.data(), 0xC0, 0x10, false);
	mpMemMgr->SetLayerName(mpRAM, "Mosaic banked RAM");
	ATMemoryHandlerTable ht {};
	ht.mpThis = this;
	ht.mbPassAnticReads = true;
	ht.mbPassReads = true;
	ht.mbPassWrites = true;
	ht.BindWriteHandler<&ATDeviceMosaic::OnWrite>();
	mpControl = mpMemMgr->CreateLayer(kATMemoryPri_HardwareOverlay,
		ht, 0xFF, 1);
	mpMemMgr->SetLayerName(mpControl, "Mosaic bank select");
	mpMemMgr->SetLayerModes(mpControl, kATMemoryAccessMode_W);
	ColdReset();
}
void ATDeviceMosaic::Shutdown() {
	if (mpMemMgr) {
		mpMemMgr->DeleteLayerPtr(&mpRAM);
		mpMemMgr->DeleteLayerPtr(&mpControl);
		mpMemMgr = nullptr;
	}
}
void ATDeviceMosaic::ColdReset() {
	std::fill(mMemory.begin(), mMemory.end(), 0);
	mBank = 0;
	UpdateMapping();
}
bool ATDeviceMosaic::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index >= 2)
		return false;
	lo = index ? 0xFF00 : 0xC000;
	hi = index ? 0xFFFF : 0xCFFF;
	return true;
}
bool ATDeviceMosaic::OnWrite(uint32 addr, uint8) {
	mBank = addr >= 0xFFC0 ? (uint8)(addr - 0xFFC0) : 0xFF;
	UpdateMapping();
	// Address decoder snoops writes; let ROM/other expansion layers see them.
	return false;
}
void ATDeviceMosaic::UpdateMapping() {
	if (!mpRAM)
		return;
	if (mBank < mBanks)
		mpMemMgr->SetLayerMemory(mpRAM, mMemory.data() + mBank * 4096);
	mpMemMgr->EnableLayer(mpRAM, mBank < mBanks);
}
vdrefptr<IATObjectState> ATDeviceMosaic::SaveState(ATSnapshotContext&) const {
	vdrefptr state { new ATSaveStateMosaic };
	state->mBank = mBank;
	state->mpMemory = new ATSaveStateMemoryBuffer;
	state->mpMemory->mpDirectName = L"mosaic-ram.bin";
	state->mpMemory->GetWriteBuffer().assign(mMemory.begin(), mMemory.end());
	return state;
}
void ATDeviceMosaic::LoadState(const IATObjectState *state, ATSnapshotContext&) {
	if (!state) {
		ColdReset();
		return;
	}
	const auto& s = atser_cast<const ATSaveStateMosaic&>(*state);
	if (!s.mpMemory || s.mpMemory->GetReadBuffer().size() != mMemory.size()
		|| (s.mBank >= 64 && s.mBank != 0xFF))
		throw ATInvalidSaveStateException();
	memcpy(mMemory.data(), s.mpMemory->GetReadBuffer().data(), mMemory.size());
	mBank = s.mBank;
	UpdateMapping();
}
