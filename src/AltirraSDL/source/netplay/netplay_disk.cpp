#include <stdafx.h>
#include "netplay_disk.h"

#include <vd2/system/error.h>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ATNetplay {

namespace {
constexpr uint32 kRollbackDiskType = 0x4B44504E; // NPDK
struct Sector {
	std::vector<uint8> data;
	ATDiskPhysicalSectorInfo info{};
};
uint64 HashSector(uint32 index, const Sector& sector) {
	uint64 hash = 0xCBF29CE484222325ULL;
	auto byte = [&](uint8 value) { hash = (hash ^ value) * 0x100000001B3ULL; };
	for (unsigned i = 0; i < 4; ++i) byte((uint8)(index >> (i * 8)));
	byte(sector.info.mFDCStatus);
	byte((uint8)sector.info.mImageSize);
	byte((uint8)(sector.info.mImageSize >> 8));
	for (uint8 value : sector.data) byte(value);
	return hash;
}
}

class DiskVersion {
public:
	std::unordered_map<uint32, std::shared_ptr<const Sector>> sectors;
	uint64 fingerprint = 0;
};

class RollbackDisk final : public vdrefcounted<IATDiskImage> {
public:
	explicit RollbackDisk(IATDiskImage* base)
		: mBase(base), mVersion(std::make_shared<DiskVersion>()) {}
	void* AsInterface(uint32 id) override {
		if (id == kRollbackDiskType) return this;
		if (id == IATDiskImage::kTypeID) return static_cast<IATDiskImage*>(this);
		return nullptr; // Geometry-changing editable-disk operations are unsafe.
	}
	ATImageType GetImageType() const override { return kATImageType_Disk; }
	std::optional<uint32> GetImageFileCRC() const override {
		return mVersion->sectors.empty() ? mBase->GetImageFileCRC() : std::nullopt;
	}
	std::optional<ATChecksumSHA256> GetImageFileSHA256() const override {
		return mVersion->sectors.empty() ? mBase->GetImageFileSHA256() : std::nullopt;
	}
	ATDiskTimingMode GetTimingMode() const override { return mBase->GetTimingMode(); }
	bool IsDirty() const override {
		return mBase->IsDirty() || !mVersion->sectors.empty();
	}
	bool IsUpdatable() const override { return false; }
	bool IsDynamic() const override { return false; }
	ATDiskImageFormat GetImageFormat() const override { return mBase->GetImageFormat(); }
	void Flush() override {
		if (IsDirty()) throw MyError("Netplay disk writes are held in the session overlay.");
	}
	void SetPath(const wchar_t*, ATDiskImageFormat) override {
		throw MyError("A speculative netplay disk cannot change its backing file.");
	}
	void Save(const wchar_t*, ATDiskImageFormat) override {
		throw MyError("A speculative netplay disk cannot be saved to a host file.");
	}
	ATDiskGeometryInfo GetGeometry() const override { return mBase->GetGeometry(); }
	uint32 GetSectorSize() const override { return mBase->GetSectorSize(); }
	uint32 GetSectorSize(uint32 index) const override { return mBase->GetSectorSize(index); }
	uint32 GetBootSectorCount() const override { return mBase->GetBootSectorCount(); }
	uint32 GetPhysicalSectorCount() const override { return mBase->GetPhysicalSectorCount(); }
	bool IsValidPhysicalSector(uint32 index) const override {
		return mBase->IsValidPhysicalSector(index);
	}
	void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const override {
		const auto found = mVersion->sectors.find(index);
		if (found != mVersion->sectors.end()) info = found->second->info;
		else mBase->GetPhysicalSectorInfo(index, info);
	}
	uint32 GetNextPhysicalSector(uint32 index) const override {
		return mBase->GetNextPhysicalSector(index);
	}
	void ReadPhysicalSector(uint32 index, void* data, uint32 len) override {
		const auto found = mVersion->sectors.find(index);
		if (found == mVersion->sectors.end()) {
			mBase->ReadPhysicalSector(index, data, len);
			return;
		}
		const auto& sector = *found->second;
		len = std::min<uint32>(len, sector.info.mPhysicalSize);
		if (len) std::memcpy(data, sector.data.data(), len);
	}
	void WritePhysicalSector(uint32 index, const void* data, uint32 len,
		uint8 status) override {
		auto sector = CopySector(index);
		len = std::min<uint32>(len, sector->info.mPhysicalSize);
		if (sector->info.mImageSize < len)
			sector->info.mImageSize = sector->info.mPhysicalSize;
		if (len) std::memcpy(sector->data.data(), data, len);
		if (len < sector->info.mImageSize)
			std::memset(sector->data.data() + len, 0, sector->info.mImageSize - len);
		sector->info.mbDirty = true;
		sector->info.mFDCStatus = status;
		Store(index, std::move(sector));
	}
	uint32 GetVirtualSectorCount() const override { return mBase->GetVirtualSectorCount(); }
	void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const override {
		mBase->GetVirtualSectorInfo(index, info);
	}
	uint32 ReadVirtualSector(uint32 index, void* data, uint32 len) override {
		ATDiskVirtualSectorInfo info;
		if (!VirtualSector(index, info)) return 0;
		ATDiskPhysicalSectorInfo physical;
		GetPhysicalSectorInfo(info.mFirstPhysSector, physical);
		len = std::min<uint32>(len, physical.mPhysicalSize);
		ReadPhysicalSector(info.mFirstPhysSector, data, len);
		return len;
	}
	bool WriteVirtualSector(uint32 index, const void* data, uint32 len) override {
		ATDiskVirtualSectorInfo info;
		if (!VirtualSector(index, info)) return false;
		auto sector = CopySector(info.mFirstPhysSector);
		const uint32 copied = std::min<uint32>(len, sector->info.mImageSize);
		if (copied) std::memcpy(sector->data.data(), data, copied);
		if (copied < sector->info.mImageSize)
			std::memset(sector->data.data() + copied, 0, sector->info.mImageSize - copied);
		sector->info.mbDirty = true;
		Store(info.mFirstPhysSector, std::move(sector));
		return true;
	}
	bool IsSafeToReinterleave() const override { return false; }
	void Reinterleave(ATDiskInterleave) override {
		throw MyError("A speculative netplay disk cannot be reinterleaved.");
	}
	std::shared_ptr<const DiskVersion> Capture() const { return mVersion; }
	uint64 Hash() const { return mVersion->fingerprint; }
	void Restore(std::shared_ptr<const DiskVersion> version) {
		// Versions are immutable once checkpointed. The next Store clones
		// the small sector index and shares unchanged sector contents.
		mVersion = std::const_pointer_cast<DiskVersion>(std::move(version));
	}
private:
	bool VirtualSector(uint32 index, ATDiskVirtualSectorInfo& info) const {
		if (index >= GetVirtualSectorCount()) return false;
		GetVirtualSectorInfo(index, info);
		return info.mNumPhysSectors != 0;
	}
	std::shared_ptr<Sector> CopySector(uint32 index) {
		if (!IsValidPhysicalSector(index)) throw MyError("Invalid netplay disk sector.");
		auto sector = std::make_shared<Sector>();
		GetPhysicalSectorInfo(index, sector->info);
		sector->data.resize(sector->info.mPhysicalSize);
		ReadPhysicalSector(index, sector->data.data(), (uint32)sector->data.size());
		return sector;
	}
	void Store(uint32 index, std::shared_ptr<Sector> sector) {
		if (!mVersion.unique()) mVersion = std::make_shared<DiskVersion>(*mVersion);
		const auto previous = mVersion->sectors.find(index);
		if (previous != mVersion->sectors.end())
			mVersion->fingerprint ^= HashSector(index, *previous->second);
		mVersion->fingerprint ^= HashSector(index, *sector);
		mVersion->sectors[index] = std::move(sector);
	}
	vdrefptr<IATDiskImage> mBase;
	std::shared_ptr<DiskVersion> mVersion;
};

bool CreateRollbackDisk(IATDiskImage* base, IATDiskImage** wrapped) {
	if (!wrapped) return false;
	*wrapped = nullptr;
	if (!base || base->IsDynamic()) return false;
	if (base->AsInterface(kRollbackDiskType)) {
		base->AddRef();
		*wrapped = base;
	} else {
		auto* proxy = new RollbackDisk(base);
		proxy->AddRef();
		*wrapped = proxy;
	}
	return true;
}

DiskCheckpoint CaptureDiskCheckpoint(IATDiskImage* image) {
	DiskCheckpoint checkpoint;
	if (image)
		if (auto* proxy = static_cast<RollbackDisk*>(image->AsInterface(kRollbackDiskType))) {
			checkpoint.image = image;
			checkpoint.version = proxy->Capture();
		}
	return checkpoint;
}

bool RestoreDiskCheckpoint(const DiskCheckpoint& checkpoint) {
	if (!checkpoint.image) return true;
	auto* proxy = static_cast<RollbackDisk*>(checkpoint.image->AsInterface(kRollbackDiskType));
	if (!proxy || !checkpoint.version) return false;
	proxy->Restore(checkpoint.version);
	return true;
}

uint64 HashRollbackDisk(IATDiskImage* image) {
	if (image)
		if (auto* proxy = static_cast<RollbackDisk*>(image->AsInterface(kRollbackDiskType)))
			return proxy->Hash();
	return 0;
}

bool SerializeDiskOverlay(IATDiskImage* image, std::vector<uint8>& out) {
	out.clear();
	const auto checkpoint = CaptureDiskCheckpoint(image);
	if (!checkpoint.image) return false;
	auto integer = [&](uint32 value, unsigned bytes) {
		for (unsigned i = 0; i < bytes; ++i) out.push_back((uint8)(value >> (i * 8)));
	};
	integer(1, 4);
	integer((uint32)checkpoint.version->sectors.size(), 4);
	// Stable ordering makes identical overlays serialize identically.
	std::vector<uint32> sectors;
	for (const auto& entry : checkpoint.version->sectors) sectors.push_back(entry.first);
	std::sort(sectors.begin(), sectors.end());
	for (uint32 index : sectors) {
		const auto& sector = *checkpoint.version->sectors.at(index);
		if (out.size() + 9 + sector.data.size() > kMaxDiskOverlayBytes) {
			out.clear();
			return false;
		}
		integer(index, 4);
		integer(sector.info.mPhysicalSize, 2);
		integer(sector.info.mImageSize, 2);
		integer(sector.info.mFDCStatus, 1);
		out.insert(out.end(), sector.data.begin(), sector.data.end());
	}
	return true;
}

bool ParseDiskOverlay(IATDiskImage* image, const uint8* data, size_t length,
	DiskCheckpoint& checkpoint) {
	checkpoint = {};
	if (!image || !data || length < 8 || length > kMaxDiskOverlayBytes
		|| !image->AsInterface(kRollbackDiskType)) return false;
	size_t position = 0;
	auto integer = [&](unsigned bytes) {
		uint32 value = 0;
		for (unsigned i = 0; i < bytes; ++i)
			value |= uint32(data[position++]) << (i * 8);
		return value;
	};
	if (integer(4) != 1) return false;
	const uint32 count = integer(4);
	if (count > image->GetPhysicalSectorCount() || count > (length - 8) / 9)
		return false;
	auto version = std::make_shared<DiskVersion>();
	for (uint32 i = 0; i < count; ++i) {
		if (length - position < 9) return false;
		const uint32 index = integer(4);
		const uint32 physicalSize = integer(2);
		const uint32 imageSize = integer(2);
		const uint8 status = (uint8)integer(1);
		if (!image->IsValidPhysicalSector(index)
			|| version->sectors.count(index) || physicalSize > length - position)
			return false;
		auto sector = std::make_shared<Sector>();
		image->GetPhysicalSectorInfo(index, sector->info);
		if (sector->info.mPhysicalSize != physicalSize || imageSize > physicalSize)
			return false;
		sector->info.mImageSize = (uint16)imageSize;
		sector->info.mFDCStatus = status;
		sector->info.mbDirty = true;
		sector->data.assign(data + position, data + position + physicalSize);
		position += physicalSize;
		version->fingerprint ^= HashSector(index, *sector);
		version->sectors[index] = std::move(sector);
	}
	if (position != length) return false;
	checkpoint.image = image;
	checkpoint.version = std::move(version);
	return true;
}

} // namespace ATNetplay
