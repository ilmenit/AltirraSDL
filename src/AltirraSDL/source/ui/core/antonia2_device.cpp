// AltirraSDL Antonia 2 arithmetic block. GPL-2.0-or-later.
// Register behavior follows upstream extras/sampledevices/antonia2-muldiv.atdevice.
// This built-in implementation never loads or executes a descriptor file.
#include <stdafx.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/snapshotimpl.h>
#include <vd2/system/registry.h>
#include <vd2/system/text.h>
#include <vd2/vdjson/jsonoutput.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <devicemanager.h>
#include <memorymanager.h>
#include <settings.h>
#include <simulator.h>
#include "antonia2_device.h"

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
		mpMem->SetLayerName(mpLayer, "Antonia 2 MULDIV block");
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
	kTag, nullptr, L"Antonia 2 MULDIV block", Create, 0
};
void Antonia2Device::GetDeviceInfo(ATDeviceInfo& info) { info.mpDef = &kDefinition; }

// Resolve the category exactly as ATExchangeSettings does, including saved
// masks and protection against cyclic profile inheritance.
uint32 FindCategoryOwner(uint32 id, ATSettingsCategory category,
	bool *cyclic = nullptr) {
	vdfastvector<uint32> seen;
	while (id) {
		if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
			if (cyclic)
				*cyclic = true;
			return 0;
		}
		seen.push_back(id);
		if (ATSettingsProfileGetCategoryMask(id)
			& ATSettingsProfileGetSavedCategoryMask(id) & category)
			return id;
		id = ATSettingsProfileGetParent(id);
	}
	return 0;
}

VDStringA ProfilePath(uint32 id) {
	return VDStringA().sprintf("Profiles\\%08X", id);
}

bool AddLegacyBlock(VDStringW& devices) {
	VDJSONDocument doc;
	VDJSONReader reader;
	const VDStringA json = VDTextWToU8(devices);
	if (!reader.Parse(json.data(), json.size(), doc) || !doc.Root().IsArray())
		return false;
	for (auto device : doc.Root().AsArray()) {
		if (!std::wcscmp(device["tag"].AsString(), L"antonia2-muldiv"))
			return true;
	}

	VDStringW migrated;
	VDJSONStringOutput output(migrated);
	VDJSONWriter writer;
	writer.Begin(&output, true);
	writer.OpenArray();
	for (auto device : doc.Root().AsArray())
		VDJSONWriteValue(writer, *device, doc.mNameTable);
	writer.OpenObject();
	writer.WriteMemberName(L"tag");
	writer.WriteStringASCII(kTag);
	writer.Close();
	writer.Close();
	writer.End();
	devices = std::move(migrated);
	return true;
}

void MigrateLegacySettings() {
	vdfastvector<uint32> ids;
	ATSettingsProfileEnum(ids);
	ids.push_back(0);
	bool hasLegacy = false;
	for (uint32 id : ids) {
		VDRegistryAppKey key(ProfilePath(id).c_str(), false);
		hasLegacy |= key.getValueType(kSetting) != VDRegistryKey::kTypeUnknown;
	}
	if (!hasLegacy)
		return;

	struct Profile {
		uint32 mId;
		uint32 mDeviceOwner;
		VDStringW mDevices;
		VDStringW mMigratedDevices;
		bool mbCyclicDeviceInheritance = false;
	};
	vdvector<Profile> profiles;
	// Snapshot all effective values before changing any inherited settings.
	for (uint32 id : ids) {
		Profile profile { id, 0, VDStringW(L"[]"), VDStringW() };
		profile.mDeviceOwner = FindCategoryOwner(id, kATSettingsCategory_Devices,
			&profile.mbCyclicDeviceInheritance);
		VDRegistryAppKey devices(ProfilePath(profile.mDeviceOwner).c_str(), false);
		devices.getString("Devices", profile.mDevices);
		if (profile.mDevices.empty())
			profile.mDevices = L"[]";
		profile.mMigratedDevices = profile.mDevices;
		VDRegistryAppKey hardware(ProfilePath(
			FindCategoryOwner(id, kATSettingsCategory_Hardware)).c_str(), false);
		// Leave malformed device lists and the legacy key intact for recovery.
		if (hardware.getBool(kSetting, false)
			&& !AddLegacyBlock(profile.mMigratedDevices))
			return;
		profiles.push_back(std::move(profile));
	}

	for (const auto& profile : profiles) {
		const bool inherited = profile.mId != profile.mDeviceOwner;
		const uint32 parentId = ATSettingsProfileGetParent(profile.mId);
		auto parent = std::find_if(profiles.begin(), profiles.end(),
			[&](const Profile& p) { return p.mId == parentId; });
		if (parent == profiles.end() || parentId == profile.mId)
			parent = std::find_if(profiles.begin(), profiles.end(),
				[](const Profile& p) { return p.mId == 0; });
		// Compare with the immediate parent's target list: it may gain its
		// own Devices category during this same migration. A cycle must
		// become explicit: equal targets within the cycle do not supply a
		// Devices owner, so the loader would still fall back to the root.
		if (!profile.mbCyclicDeviceInheritance
			&& profile.mMigratedDevices == (inherited
				? parent->mMigratedDevices : profile.mDevices))
			continue;
		VDRegistryAppKey key(ProfilePath(profile.mId).c_str());
		if (!key.setString("Devices", profile.mMigratedDevices.c_str()))
			return;
		if (inherited) {
			// Hardware and Devices can have different inheritance. Preserve
			// the old effective configuration by giving this profile its own
			// Devices category only when the migrated lists would differ.
			ATSettingsProfileSetCategoryMask(profile.mId,
				(ATSettingsCategory)(ATSettingsProfileGetCategoryMask(profile.mId)
					| kATSettingsCategory_Devices));
			ATSettingsProfileSetSavedCategoryMask(profile.mId,
				(ATSettingsCategory)(ATSettingsProfileGetSavedCategoryMask(profile.mId)
					| kATSettingsCategory_Devices));
		}
	}
	for (uint32 id : ids) {
		VDRegistryAppKey key(ProfilePath(id).c_str());
		key.removeValue(kSetting);
	}
}
}

void ATUIAntonia2DeviceInit() {
	g_sim.GetDeviceManager()->AddDeviceDefinition(&kDefinition);
	// Fork extension: expose the upstream sample's arithmetic block as a
	// built-in device. It is not a complete Antonia 2 accelerator model.
	MigrateLegacySettings();
}
