// AltirraSDL Antonia 2 arithmetic block. GPL-2.0-or-later.
// Register behavior follows upstream extras/sampledevices/antonia2-muldiv.atdevice.
// This built-in implementation never loads or executes a descriptor file.
#include <stdafx.h>
#include <cstring>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include <vd2/system/registry.h>
#include <devicemanager.h>
#include <memorymanager.h>
#include <settings.h>
#include <simulator.h>
#include "antonia2_settings.h"

extern ATSimulator g_sim;

namespace {
constexpr char kTag[] = "antonia2-muldiv";
constexpr char kSetting[] = "CPU: Antonia2 MULDIV Block";

struct Antonia2State final : ATSnapExchangeObject<Antonia2State, "Antonia2MULDIV"> {
	uint8 mOperands[8] {};
	uint8 mResults[8] {};
	template<ATExchanger T> void Exchange(T& ex) {
		ex.TransferArray("operands", mOperands);
		ex.TransferArray("results", mResults);
	}
};

class Antonia2Device final : public ATDeviceT<IATDeviceMemMap, IATDeviceSnapshot> {
public:
	~Antonia2Device() { Shutdown(); }
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void InitMemMap(ATMemoryManager *mem) override { mpMem = mem; }
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override {
		if (index) return false;
		lo = 0xFFF008;
		hi = 0xFFF00F;
		return true;
	}
	void Init() override {
		ATMemoryHandlerTable handlers {};
		handlers.mpThis = this;
		handlers.mbPassReads = handlers.mbPassWrites = handlers.mbPassAnticReads = true;
		handlers.BindReadHandler<&Antonia2Device::Read>();
		handlers.BindDebugReadHandler<&Antonia2Device::Read>();
		handlers.BindWriteHandler<&Antonia2Device::Write>();
		mpLayer = mpMem->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xFFF0, 1);
		mpMem->SetLayerName(mpLayer, "Antonia2 MULDIV Block");
		mpMem->SetLayerFastBus(mpLayer, true);
		mpMem->EnableLayer(mpLayer, kATMemoryAccessMode_RW, true);
	}
	void Shutdown() override {
		if (mpLayer) {
			mpMem->DeleteLayer(mpLayer);
			mpLayer = nullptr;
		}
		mpMem = nullptr;
	}
	void ColdReset() override {
		std::memset(mOperands, 0xFF, sizeof mOperands);
		std::memset(mResults, 0xFF, sizeof mResults);
	}
	void ComputerColdReset() override { ColdReset(); }
	void LoadState(const IATObjectState *state, ATSnapshotContext&) override {
		if (!state) { ColdReset(); return; }
		const auto& saved = atser_cast<const Antonia2State&>(*state);
		std::memcpy(mOperands, saved.mOperands, sizeof mOperands);
		std::memcpy(mResults, saved.mResults, sizeof mResults);
	}
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext&) const override {
		vdrefptr state { new Antonia2State };
		std::memcpy(state->mOperands, mOperands, sizeof mOperands);
		std::memcpy(state->mResults, mResults, sizeof mResults);
		return state;
	}
private:
	sint32 Read(uint32 address) const {
		return address >= 0xFFF008 && address <= 0xFFF00F
			? mResults[address - 0xFFF008] : -1;
	}
	bool Write(uint32 address, uint8 value) {
		if (address < 0xFFF008 || address > 0xFFF00F) return false;
		const unsigned index = address - 0xFFF008;
		mOperands[index] = value;
		const unsigned base = index & 4;
		const uint32 x = uint32(mOperands[base]) | (uint32(mOperands[base + 1]) << 8);
		const uint32 y = uint32(mOperands[base + 2]) | (uint32(mOperands[base + 3]) << 8);
		if (base) {
			const uint32 product = x * y;
			for (unsigned i = 0; i < 4; ++i)
				mResults[4 + i] = uint8(product >> (8 * i));
		} else {
			const uint32 quotient = y ? x / y : 0xFFFF;
			const uint32 remainder = y ? x % y : x;
			mResults[0] = uint8(quotient);
			mResults[1] = uint8(quotient >> 8);
			mResults[2] = uint8(remainder);
			mResults[3] = uint8(remainder >> 8);
		}
		return true;
	}
	ATMemoryManager *mpMem = nullptr;
	ATMemoryLayer *mpLayer = nullptr;
	uint8 mOperands[8] {};
	uint8 mResults[8] {};
};

void Create(const ATPropertySet&, IATDevice **dev) {
	vdrefptr device { new Antonia2Device };
	*dev = device.release();
}
const ATDeviceDefinition kDefinition {
	kTag, nullptr, L"Antonia2 MULDIV Block", Create,
	kATDeviceDefFlag_Internal | kATDeviceDefFlag_Hidden
};
void Antonia2Device::GetDeviceInfo(ATDeviceInfo& info) { info.mpDef = &kDefinition; }

ATSettingsLoadSaveCallback gLoad, gSave;
bool gRegistered = false;
}

bool ATUIGetAntonia2Enabled() {
	return g_sim.GetDeviceManager()->GetDeviceByTag(kTag) != nullptr;
}

void ATUISetAntonia2Enabled(bool enabled) {
	auto *manager = g_sim.GetDeviceManager();
	auto *device = manager->GetDeviceByTag(kTag);
	if (enabled && !device)
		manager->AddDevice(&kDefinition, ATPropertySet());
	else if (!enabled && device)
		manager->RemoveDevice(device);
}

void ATUIAntonia2SettingsInit() {
	if (gRegistered) return;
	g_sim.GetDeviceManager()->AddDeviceDefinition(&kDefinition);
	gLoad = [](uint32, ATSettingsCategory mask, VDRegistryKey& key) {
		if (mask & kATSettingsCategory_Hardware)
			ATUISetAntonia2Enabled(key.getBool(kSetting, false));
	};
	gSave = [](uint32, ATSettingsCategory mask, VDRegistryKey& key) {
		if (mask & kATSettingsCategory_Hardware)
			key.setBool(kSetting, ATUIGetAntonia2Enabled());
	};
	ATSettingsRegisterLoadCallback(&gLoad);
	ATSettingsRegisterSaveCallback(&gSave);
	gRegistered = true;
}

void ATUIAntonia2SettingsShutdown() {
	if (!gRegistered) return;
	ATSettingsUnregisterLoadCallback(&gLoad);
	ATSettingsUnregisterSaveCallback(&gSave);
	gRegistered = false;
}
