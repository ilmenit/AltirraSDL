#include <stdafx.h>
#include "netplay_disk.h"
#include <vd2/system/error.h>
#include <array>
#include <cstdio>
#include <cstdlib>

// The standalone image test must never read or persist an emulator profile.
// The POSIX system library references these application-owned boundaries.
void ATUILoadRegistry(const wchar_t*) { std::abort(); }
void ATUISaveRegistry(const wchar_t*) { std::abort(); }

using namespace ATNetplay;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", \
	__LINE__, #x); ++failures; } } while (0)

int main() {
	vdrefptr<IATDiskImage> original;
	ATCreateDiskImage(720, 3, 128, ~original);
	std::array<uint8, 128> data{};
	data.fill(0x31);
	CHECK(original->WriteVirtualSector(0, data.data(), (uint32)data.size()));
	vdrefptr<IATDiskImage> disk;
	CHECK(CreateRollbackDisk(original, ~disk));
	CHECK(!disk->IsUpdatable());
	const auto first = CaptureDiskCheckpoint(disk);
	data.fill(0x52);
	CHECK(disk->WriteVirtualSector(0, data.data(), (uint32)data.size()));
	const auto second = CaptureDiskCheckpoint(disk);
	data.fill(0);
	original->ReadVirtualSector(0, data.data(), (uint32)data.size());
	CHECK(data[0] == 0x31); // speculative writes never touch the base
	disk->ReadVirtualSector(0, data.data(), (uint32)data.size());
	CHECK(data[0] == 0x52);
	CHECK(RestoreDiskCheckpoint(first));
	disk->ReadVirtualSector(0, data.data(), (uint32)data.size());
	CHECK(data[0] == 0x31 && disk->IsDirty() == original->IsDirty());
	CHECK(RestoreDiskCheckpoint(second));
	disk->ReadVirtualSector(0, data.data(), (uint32)data.size());
	CHECK(data[0] == 0x52 && disk->IsDirty());

	// Branching from a historical version must not modify either saved
	// future. Partial writes zero-fill exactly like ATDiskImage.
	CHECK(RestoreDiskCheckpoint(first));
	data.fill(0x73);
	CHECK(disk->WriteVirtualSector(0, data.data(), 8));
	disk->ReadVirtualSector(0, data.data(), (uint32)data.size());
	CHECK(data[0] == 0x73 && data[7] == 0x73 && data[8] == 0);
	CHECK(RestoreDiskCheckpoint(second));
	disk->ReadVirtualSector(0, data.data(), (uint32)data.size());
	CHECK(data[0] == 0x52 && data[127] == 0x52);

	// Physical-sector status belongs to the same rollback version.
	ATDiskVirtualSectorInfo mapping;
	disk->GetVirtualSectorInfo(0, mapping);
	disk->WritePhysicalSector(mapping.mFirstPhysSector, data.data(), 8, 0xEF);
	ATDiskPhysicalSectorInfo physical;
	disk->GetPhysicalSectorInfo(mapping.mFirstPhysSector, physical);
	CHECK(physical.mFDCStatus == 0xEF && physical.mbDirty);
	CHECK(RestoreDiskCheckpoint(second));
	disk->GetPhysicalSectorInfo(mapping.mFirstPhysSector, physical);
	CHECK(physical.mFDCStatus != 0xEF);
	const uint64 fingerprint = HashRollbackDisk(disk);
	std::vector<uint8> bytes;
	CHECK(SerializeDiskOverlay(disk, bytes));
	vdrefptr<IATDiskImage> recipient;
	CHECK(CreateRollbackDisk(original, ~recipient));
	DiskCheckpoint received;
	CHECK(ParseDiskOverlay(recipient, bytes.data(), bytes.size(), received));
	CHECK(RestoreDiskCheckpoint(received));
	CHECK(HashRollbackDisk(recipient) == fingerprint);
	recipient->ReadVirtualSector(0, data.data(), 128);
	CHECK(data[0] == 0x52 && data[127] == 0x52);
	// Truncated and malformed sidecars never mutate the destination.
	for (size_t length = 0; length < bytes.size(); ++length)
		CHECK(!ParseDiskOverlay(recipient, bytes.data(), length, received));
	bytes[8] = bytes[9] = bytes[10] = bytes[11] = 0xFF;
	CHECK(!ParseDiskOverlay(recipient, bytes.data(), bytes.size(), received));
	CHECK(HashRollbackDisk(recipient) == fingerprint);

	// Native images keep their original behavior and don't acquire a
	// fake rollback token. Invalid virtual writes are rejected normally.
	CHECK(!CaptureDiskCheckpoint(original).image);
	CHECK(!disk->WriteVirtualSector(99999, data.data(), 128));
	CHECK(disk->ReadVirtualSector(99999, data.data(), 128) == 0);
	bool blocked = false;
	try { disk->Flush(); } catch (const MyError&) { blocked = true; }
	CHECK(blocked);

	return failures ? 1 : 0;
}
