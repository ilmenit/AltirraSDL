#include <stdafx.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <at/atcore/propertyset.h>
#include <at/atcore/snapshot.h>
#include <vd2/system/file.h>
#include <vd2/system/zip.h>
#include "af80.h"
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "mosaic.h"
#include "savestateio.h"

static void Check(bool value, const char *message) {
	if (!value) { fprintf(stderr, "%s\n", message); std::exit(1); }
}

// Firmware service fixture supplies known bytes rather than shipping board
// ROMs. Production firmware discovery uses the normal firmware manager.
static int gFirmwareMode = 0;
ATFirmwareManager::ATFirmwareManager() {}
ATFirmwareManager::~ATFirmwareManager() {}
uint64 ATFirmwareManager::GetCompatibleFirmware(ATFirmwareType type) const {
	return type;
}
bool ATFirmwareManager::LoadFirmware(uint64 id, void *dst, uint32 offset,
	uint32 len, bool *changed, uint32 *actualLen, vdfastvector<uint8> *,
	const uint8 *, bool *usable) const
{
	Check(offset == 0 && len == 4096, "Wrong AF80 firmware geometry");
	if (gFirmwareMode == 1)
		return false;
	auto *p = (uint8 *)dst;
	if (id == kATFirmwareType_AF80Firmware) {
		for (unsigned i = 0; i < len; ++i)
			p[i] = (uint8)(i / 256 + 0xA0);
	} else {
		Check(id == kATFirmwareType_AF80Charset, "Wrong firmware type");
		memset(p, 0, len);
		for (unsigned line = 0; line < 10; ++line)
			p['A' * 16 + line] = 0x01;
		// Original synthetic five-column E: LSB is the left edge. Keep
		// the asymmetric stem so mirrored character rendering fails.
		const uint8 e[] = {0x1F, 0x01, 0x07, 0x01, 0x1F};
		memcpy(p + 'E' * 16, e, sizeof e);
	}
	if (changed) *changed = true;
	if (actualLen) *actualLen = gFirmwareMode == 2 ? len - 1 : len;
	if (usable) *usable = true;
	return true;
}
void ATConsoleWrite(const char *) { Check(false, "Unexpected console write"); }

static vdrefptr<IATObjectState> RoundTrip(IATObjectState *state) {
	VDMemoryBufferStream data;
	{
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(data));
		vdautoptr<IATSaveStateSerializer> writer(ATCreateSaveStateSerializer(L"expansion.json"));
		writer->Serialize(*zip, *state);
		zip->Finalize();
	}
	data.Seek(0);
	VDZipArchive zip;
	zip.Init(&data);
	vdautoptr<IATSaveStateDeserializer> reader(ATCreateSaveStateDeserializer(L"expansion.json"));
	vdrefptr<IATSerializable> restored;
	reader->Deserialize(zip, ~restored);
	Check(restored && &restored->GetSerializationType() == &state->GetSerializationType(),
		"Snapshot type lost during serialization");
	return vdrefptr<IATObjectState>(static_cast<IATObjectState *>(restored.get()));
}

static void TestMosaic() {
	ATMemoryManager mem;
	mem.Init();
	uint8 base[65536];
	memset(base, 0x5A, sizeof base);
	auto *layer = mem.CreateLayer(0, base, 0, 256, false);
	mem.EnableLayer(layer, true);
	auto *rom = mem.CreateLayer(kATMemoryPri_ROM, base + 0xFF00, 0xFF, 1, true);
	mem.EnableLayer(rom, true);
	ATDeviceMosaic dev;
	ATPropertySet props;
	props.SetUint32("banks", 36);
	dev.SetSettings(props);
	dev.InitMemMap(&mem);
	dev.Init();
	Check(mem.CPUReadByte(0xC000) == 0, "Mosaic power-on RAM unavailable");
	for (unsigned bank = 0; bank < 36; ++bank) {
		mem.CPUWriteByte(0xFFC0 + bank, (uint8)(255 - bank));
		mem.CPUWriteByte(0xC000, bank + 1);
		mem.CPUWriteByte(0xCFFF, bank + 37);
		Check(mem.CPUReadByte(0xFFC0 + bank) == 0x5A,
			"Mosaic bank write modified OS ROM");
	}
	for (unsigned bank = 0; bank < 36; ++bank) {
		mem.CPUWriteByte(0xFFC0 + bank, 0);
		Check(mem.CPUReadByte(0xC000) == bank + 1
			&& mem.AnticReadByte(0xCFFF) == bank + 37,
			"Mosaic banks alias or ANTIC mapping incorrect");
	}
	mem.CPUWriteByte(0xFFE3, 0);
	dev.WarmReset();
	Check(mem.CPUReadByte(0xC000) == 36, "Mosaic warm reset lost bank");
	ATSnapshotContext ctx {};
	auto state = RoundTrip(dev.SaveState(ctx));
	dev.ColdReset();
	Check(mem.CPUReadByte(0xC000) == 0, "Mosaic cold reset did not clear RAM");
	dev.LoadState(state, ctx);
	Check(mem.CPUReadByte(0xC000) == 36, "Mosaic save state lost selected bank");
	mem.CPUWriteByte(0xFFE4, 0);
	Check(mem.CPUReadByte(0xC000) == 0x5A, "Mosaic uninstalled bank did not deselect");
	mem.CPUWriteByte(0xFFC0, 0);
	mem.CPUWriteByte(0xFF00, 0);
	Check(mem.CPUReadByte(0xC000) == 0x5A, "Mosaic FF page decode incomplete");
	dev.Shutdown();
	mem.DeleteLayer(rom);
	mem.DeleteLayer(layer);
}

static uint32 Pixel(ATDeviceAF80& dev, unsigned x, unsigned y) {
	dev.UpdateFrame();
	const auto& frame = dev.GetFrameBuffer();
	return ((const uint32 *)((const char *)frame.data + y * frame.pitch))[x];
}
static void TestAF80() {
	ATMemoryManager mem;
	mem.Init();
	uint8 base[65536];
	memset(base, 0x5A, sizeof base);
	auto *layer = mem.CreateLayer(0, base, 0, 256, false);
	mem.EnableLayer(layer, true);
	ATFirmwareManager firmware;
	ATDeviceAF80 dev;
	dev.InitMemMap(&mem);
	dev.InitFirmware(&firmware);
	dev.Init();
	Check(dev.GetFirmwareStatus() == ATDeviceFirmwareStatus::OK,
		"AF80 firmware rejected");
	Check(dev.GetVideoInfo().mbSignalPassThrough,
		"AF80 power-on output must select 40-column passthrough");
	gFirmwareMode = 1;
	dev.ReloadFirmware();
	Check(dev.GetFirmwareStatus() == ATDeviceFirmwareStatus::Missing,
		"AF80 missing firmware not reported");
	gFirmwareMode = 2;
	dev.ReloadFirmware();
	Check(dev.GetFirmwareStatus() == ATDeviceFirmwareStatus::Invalid,
		"AF80 truncated firmware not rejected");
	gFirmwareMode = 0;
	dev.ReloadFirmware();
	dev.SetCartEnables(true, false, false);
	Check(mem.CPUReadByte(0x8000) == 0x5A
		&& mem.CPUReadByte(0xD500) == 0x5A,
		"AF80 ignored cartridge port enable gating");
	dev.SetCartEnables(true, true, true);
	for (unsigned bank = 0; bank < 16; ++bank) {
		mem.CPUWriteByte(0xD5F7, bank);
		Check(mem.CPUReadByte(0xD500) == 0xA0 + bank
			&& mem.CPUReadByte(0x8000) == 0xA0 + bank
			&& mem.CPUReadByte(0x9FFF) == 0xA0 + bank,
			"AF80 ROM bank/mirror wrong");
		Check(mem.CPUReadByte(0xD5EF) == 0xA0 + bank
			&& mem.CPUReadByte(0xD5F8) == 0xA0 + bank,
			"AF80 register hole swallowed adjacent ROM bytes");
		for (unsigned address = 0xD5F0; address <= 0xD5F7; ++address)
			Check(mem.CPUReadByte(address) == 0x5A
				&& mem.CPUDebugReadByte(address) == 0x5A,
				"AF80 write-only register holes drive the read bus");
	}
	mem.CPUWriteByte(0xD5F7, 0x10);
	Check(mem.CPUReadByte(0x8000) == 255, "AF80 ROM disable wrong");
	Check(mem.CPUReadByte(0xD500) == 0x5A,
		"AF80 disabled ROM did not release the CCTL read bus");
	mem.CPUWriteByte(0xD5F7, 0x20);
	Check(mem.CPUReadByte(0x8000) == 0x5A, "AF80 RD4 disable did not reveal RAM");
	mem.CPUWriteByte(0x8000, 0x69);
	mem.CPUWriteByte(0xD5F7, 0);
	mem.CPUWriteByte(0x8000, 0x12);
	mem.CPUWriteByte(0xD5F7, 0x20);
	Check(mem.CPUReadByte(0x8000) == 0x69, "AF80 cartridge writes damaged base RAM");
	for (unsigned bank = 0; bank < 16; ++bank) {
		mem.CPUWriteByte(0xD5F6, bank);
		mem.CPUWriteByte(0xD600, bank + 1);
		Check(mem.CPUReadByte(0xD680) == bank + 1, "AF80 VRAM mirror wrong");
	}
	for (unsigned bank = 0; bank < 16; ++bank) {
		mem.CPUWriteByte(0xD5F6, bank);
		Check(mem.CPUReadByte(0xD600) == bank + 1, "AF80 VRAM banks alias");
	}
	// Hide cursor, use top-screen base zero and no split inside 25 rows.
	mem.CPUWriteByte(0xD5F6, 0x30);
	mem.CPUWriteByte(0xD690, 25);
	mem.CPUWriteByte(0xD618, 255);
	Check(mem.CPUReadByte(0xD610) == 25 && mem.CPUReadByte(0xD690) == 25,
		"AF80 CRTC register foldback wrong");
	Check(mem.CPUReadByte(0xD63A) == 1 && mem.CPUReadByte(0xD6BA) == 1,
		"AF80 ready register foldback wrong");
	mem.CPUWriteByte(0xD5F6, 0);
	mem.CPUWriteByte(0xD600, 'A');
	mem.CPUWriteByte(0xD601, 'E');
	mem.CPUWriteByte(0xD5F6, 0x90);
	mem.CPUWriteByte(0xD600, 0xF0);
	mem.CPUWriteByte(0xD601, 0xF0);
	Check(Pixel(dev, 8, 1) == 0xFFFFFF && Pixel(dev, 9, 1) == 0
		&& Pixel(dev, 15, 1) == 0 && Pixel(dev, 12, 0) == 0xFFFFFF
		&& Pixel(dev, 13, 0) == 0,
		"AF80 character ROM dots must render left to right from bit 0");
	Check(Pixel(dev, 0, 0) == 0xFFFFFF && Pixel(dev, 1, 0) == 0,
		"AF80 glyph raster/attribute color wrong");
	mem.CPUWriteByte(0xD600, 0x10);
	Check(Pixel(dev, 0, 0) == 0xAA0000, "AF80 RGBI red/blue wiring wrong");
	mem.CPUWriteByte(0xD600, 0xF1);
	Check(Pixel(dev, 0, 0) == 0 && Pixel(dev, 1, 0) == 0xFFFFFF,
		"AF80 reverse video wrong");
	mem.CPUWriteByte(0xD600, 0xF2);
	dev.Tick(150);
	Check(Pixel(dev, 0, 0) == 0, "AF80 blink did not blank glyph");
	mem.CPUWriteByte(0xD600, 0xF6);
	Check(Pixel(dev, 1, 9) == 0xFFFFFF, "AF80 underline priority wrong");
	ATSnapshotContext ctx {};
	auto state = RoundTrip(dev.SaveState(ctx));
	dev.ColdReset();
	dev.LoadState(state, ctx);
	Check(Pixel(dev, 1, 9) == 0xFFFFFF, "AF80 snapshot lost attributes or blink");
	uint8 ch;
	Check(dev.ReadRawText(&ch, 0, 0, 1) == 1 && ch == 'A',
		"AF80 text extraction wrong");
	mem.CPUWriteByte(0xD5F6, 0);
	Check(dev.GetVideoInfo().mbSignalPassThrough && Pixel(dev, 0, 0) == 0,
		"AF80 output disable wrong");
	dev.Shutdown();
	mem.DeleteLayer(layer);
}
int main() {
	TestMosaic();
	TestAF80();
	puts("Mosaic and Austin Franklin regressions passed");
}
