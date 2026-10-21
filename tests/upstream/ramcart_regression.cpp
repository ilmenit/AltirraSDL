#include <stdafx.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <filesystem>
#include <at/atcore/device.h>
#include <at/atcore/snapshot.h>
#include <vd2/system/file.h>
#include <vd2/system/zip.h>
#include "cartridge.h"
#include "memorymanager.h"
#include "options.h"
#include "savestateio.h"
#include <vd2/system/registrymemory.h>
#include "../../src/AltirraSDL/source/ui/core/ramcart_settings_storage.h"

static void Check(bool ok, const char *message) {
	if (!ok) {
		fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

// This test never executes flash mapper selection; options are a test
// service with default-constructed chip-name strings only.
ATOptionsData::ATOptionsData() = default;
ATOptions g_ATOptions;

// Resource/UI services are not exercised by RAM cartridge emulation.
// Fail loudly if a tested mapper unexpectedly reaches either path.
bool ATLoadKernelResource(int, void *, uint32, uint32, bool) {
	Check(false, "Unexpected kernel-resource access");
	return false;
}
void ATConsoleWrite(const char *) { Check(false, "Unexpected console output"); }
VDStringW ATSettingsGetDefaultPortablePath() { Check(false, "Unexpected settings migration"); return {}; }
bool ATSettingsIsMigrationScheduled() { Check(false, "Unexpected settings migration"); return false; }
bool ATSettingsIsInPortableMode() { Check(false, "Unexpected settings mode query"); return false; }

struct Services final : IATDeviceManager, IATDeviceSchedulingService,
	IATDeviceCartridgePort {
	ATMemoryManager memory;
	ATScheduler scheduler;
	uint8 ram[65536] {};
	ATMemoryLayer *layer = nullptr;
	bool rd5 = false;

	Services() {
		memory.Init();
		layer = memory.CreateLayer(0, ram, 0, 0x100, false);
		memory.EnableLayer(layer, true);
	}
	~Services() { memory.DeleteLayer(layer); }
	void *GetService(uint32 iid) override {
		if (iid == ATMemoryManager::kTypeID) return &memory;
		if (iid == IATDeviceSchedulingService::kTypeID)
			return static_cast<IATDeviceSchedulingService *>(this);
		return nullptr;
	}
	void NotifyDeviceStatusChanged(IATDevice&) override {}
	ATScheduler *GetMachineScheduler() const override { return const_cast<ATScheduler *>(&scheduler); }
	ATScheduler *GetSlowScheduler() const override { return const_cast<ATScheduler *>(&scheduler); }
	void AddCartridge(IATDeviceCartridge *cart, ATCartridgePriority, uint32& id) override {
		id = 1;
		cart->SetCartEnables(true, true, true);
	}
	void RemoveCartridge(uint32, IATDeviceCartridge *) override {}
	bool IsLeftWindowEnabled(uint32) const override { return true; }
	bool IsRightWindowEnabled(uint32) const override { return true; }
	bool IsCCTLEnabled(uint32) const override { return true; }
	void EnablePassThrough(uint32, bool, bool, bool) override {}
	void OnLeftWindowChanged(uint32, bool active) override { rd5 = active; }
	bool IsLeftWindowActiveDownstream(uint32) const override { return false; }
};

static vdrefptr<IATObjectState> RoundTrip(IATObjectState *state) {
	VDMemoryBufferStream data;
	{
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(data));
		vdautoptr<IATSaveStateSerializer> writer(ATCreateSaveStateSerializer(L"cart.json"));
		writer->Serialize(*zip, *state);
		zip->Finalize();
	}
	data.Seek(0);
	VDZipArchive zip;
	zip.Init(&data);
	vdautoptr<IATSaveStateDeserializer> reader(ATCreateSaveStateDeserializer(L"cart.json"));
	vdrefptr<IATSerializable> restored;
	reader->Deserialize(zip, ~restored);
	Check(restored && &restored->GetSerializationType() == &state->GetSerializationType(),
		"Serialized cartridge snapshot has wrong type");
	return vdrefptr<IATObjectState>(static_cast<IATObjectState *>(restored.get()));
}

int main() {
	const uint32 sizes[] = {64,128,256,1024,2048,4096,8192,16384,32768,32};
	for (int mapper = 94; mapper <= 103; ++mapper) {
		const auto mode = ATGetCartridgeModeForMapper(mapper);
		const uint32 size = sizes[mapper-94] * 1024;
		Check(mode != kATCartridgeMode_None && ATGetImageSizeForCartridgeType(mode) == size,
			"RAM cartridge mapper/size missing");
		Check(ATGetCartridgeMapperForMode(mode, size) == mapper, "Mapper roundtrip failed");
		Services services;
		ATCartridgeEmulator cart(100, kATCartridgePriority_Default, false);
		cart.SetManager(&services);
		cart.InitCartridge(&services);
		cart.Init();
		cart.LoadNewCartridge(mode);
		auto& mem = services.memory;
		auto write = [&](uint16 address, uint8 value) { mem.CPUWriteByte(address, value); };
		auto read = [&](uint16 address) { return mem.CPUReadByte(address); };
		if (mapper == 103) {
			Check(!services.rd5, "SiDiCar must not assert RD5");
			for (int bank = 0; bank < 4; ++bank) {
				write(0xD5FF, 0x10 | bank);
				write(0x8000, 0x40 + bank);
				write(0x9FFF, 0x80 + bank);
			}
			for (int bank = 0; bank < 4; ++bank) {
				write(0xD500, 0x10 | bank);
				Check(read(0x8000) == 0x40 + bank && read(0x9FFF) == 0x80 + bank,
					"SiDiCar banks overlap or fail to retain both ends");
			}
			write(0xD5FF, 0x80);
			Check(read(0x8000) == 0x43, "SiDiCar bit7 write inhibition failed");
			write(0xD5FF, 0);
			write(0x8000, 0x24);
			Check(services.ram[0x8000] == 0x24, "Disabled SiDiCar must reveal host RAM");
			write(0xD5FF, 0x13);
		} else {
			Check(services.rd5, "RAMCart power-up read-only RD5 missing");
			write(0xA000, 0x33);
			Check(read(0xA000) == 0xFF, "Read-only SRAM was modified");
			cart.SetRamCartSwitches(0x1000 | (mapper == 96 ? 0x2000 : 0)
				| (mapper >= 97 && mapper <= 99 ? 0x8000 : 0));
			Check(!services.rd5, "R/W switch must invert upper-window enable");
			// Exercise every physical bank, including top address bits. Expected
			// offsets are independent 16K-bank numbers from the documented wiring.
			const uint32 banks = size >> 14;
			for (uint32 bank = 0; bank < banks; ++bank) {
				if (mapper == 98 || mapper == 99)
					cart.SetRamCartSwitches(0x9000 | ((bank >> 6) << 8));
				if (mapper >= 100)
					write(0xD501, bank >> 6);
				uint8 control = 3 | ((bank & 7) << 3);
				if (mapper >= 96) control |= ((bank >> 3) & 1) << 2;
				if (mapper >= 97) control |= ((bank >> 4) & 3) << 6;
				write(0xD500, control);
				write(0x8000, (uint8)bank);
				write(0xBFFF, (uint8)(bank >> 8));
				const auto *image = static_cast<const uint8 *>(cart.GetImage()->GetBuffer());
				Check(image[bank * 16384] == (uint8)bank
					&& image[bank * 16384 + 16383] == (uint8)(bank >> 8),
					"RAMCart bank wiring/address wrap is wrong");
			}
			if (mapper <= 95) {
				write(0xD5AA, 7);
				write(0xD500, 0);
				Check(cart.GetCartBank() & 4, "RAMCart lock failed");
				cart.ColdReset();
				Check(!(cart.GetCartBank() & 4), "Power reset did not clear RAMCart lock");
				write(0xD500, 3);
			}
			Check(services.rd5, "Upper window enabled but RD5 missing");
		}
		// The debugger bank namespace is limited to 256 banks; every
		// representable physical bank must expose all mapped 4K pages.
		const uint32 debugShift = mapper == 103 ? 13 : 14;
		const uint32 debugBanks = std::min<uint32>(size >> debugShift, 256);
		for (uint32 bank = 0; bank < debugBanks; ++bank) {
			for (uint32 page = 0; page < (mapper == 103 ? 2U : 4U); ++page) {
				const uint32 address = 0x8000 + page * 4096;
				for (uint32 edge : {0U, 4095U}) {
					const uint32 linear = (bank << debugShift) + page * 4096 + edge;
					Check(cart.DebugReadBanked((bank << 16) | (address + edge))
						== static_cast<const uint8 *>(cart.GetImage()->GetBuffer())[linear],
						"Debugger banked read missed RAM cartridge data");
				}
			}
		}
		if (debugBanks < 256)
			Check(cart.DebugReadBanked((debugBanks << 16) | 0x8000) == 0,
				"Debugger exposed an uninstalled RAM cartridge bank");
		if (mapper == 103)
			Check(cart.DebugReadBanked(0xA000) == 0,
				"Debugger exposed a nonexistent SiDiCar upper window");
		Check(cart.DebugReadLinear(size - 1)
			== static_cast<const uint8 *>(cart.GetImage()->GetBuffer())[size - 1],
			"Debugger linear read lost the end of a large RAM cartridge");
		Check(cart.IsDirty() && cart.GetImage()->IsDirty(), "SRAM writes must dirty the media");
		vdrefptr<IATObjectState> state;
		cart.SaveState(~state);
		const auto saved = RoundTrip(state);
		const int savedBank = cart.GetCartBank();
		const uint8 before = read(0x8000);
		write(0x8000, before ^ 0xFF);
		cart.ColdReset();
		cart.LoadState(*saved);
		Check(cart.GetCartBank() == savedBank && read(0x8000) == before,
			"Serialized snapshot did not restore bank, switches and mutable image");
		const auto path = std::filesystem::temp_directory_path()
			/ ("altirra-ramcart-" + std::to_string(mapper) + "-"
				+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".car");
		const auto widePath = path.wstring();
		cart.Save(widePath.c_str(), true);
		Check(!cart.IsDirty(), "Saving SRAM image did not clear dirty flag");
		vdrefptr<IATCartridgeImage> loaded;
		Check(ATLoadCartridgeImage(widePath.c_str(), ~loaded), "Saved CART image failed to reload");
		std::filesystem::remove(path);
		Check(loaded->GetMode() == mode && loaded->GetImageSize() == size
			&& !memcmp(loaded->GetBuffer(), cart.GetImage()->GetBuffer(), size),
			"CART save/reload lost mapper or modified SRAM data");
		cart.Shutdown();
	}
	// Profile A enables writes and upper module selection; a fresh profile B
	// must reset to factory settings rather than inherit A's mutable defaults.
	IVDRegistryProvider *previousProvider = VDGetRegistryProvider();
	VDRegistryProviderMemory provider;
	VDSetRegistryProvider(&provider);
	{
		VDRegistryKey profileA("ramcart-regression/profileA");
		VDRegistryKey profileB("ramcart-regression/profileB");
		ATSetRamCartDefaultSwitches(kATCartridgeMode_RamCart_4M, 0x13C4);
		ATUIExchangeRamCartSwitchDefaults(true, profileA);
		ATUIExchangeRamCartSwitchDefaults(false, profileB);
		Check(ATGetRamCartDefaultSwitches(kATCartridgeMode_RamCart_4M) == 0,
			"Fresh profile leaked physical switches from previous profile");
		ATUIExchangeRamCartSwitchDefaults(false, profileA);
		Check(ATGetRamCartDefaultSwitches(kATCartridgeMode_RamCart_4M) == 0x13C4,
			"Returning to saved profile lost physical switches");
		Services services;
		ATCartridgeEmulator cart(100, kATCartridgePriority_Default, false);
		cart.SetManager(&services);
		cart.InitCartridge(&services);
		cart.Init();
		cart.LoadNewCartridge(kATCartridgeMode_RamCart_4M);
		Check(cart.GetRamCartSwitches() == 0x13C4,
			"New cartridge did not apply profile physical defaults");
		cart.SetRamCartSwitches(0x9000);
		services.memory.CPUWriteByte(0xD500, 0xC7);
		cart.SetRamCartSwitches(0x13C4);
		Check(cart.GetRamCartSwitches() == 0x13C4,
			"Switching from software ABC jumpers lost physical module defaults");
		cart.Shutdown();
	}
	VDSetRegistryProvider(previousProvider);
	puts("RAMCart/SiDiCar mapper, memory bus, write protection and snapshots passed");
}
