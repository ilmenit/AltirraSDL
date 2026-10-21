// ZIP64 index contract: virtual sparse >4GiB archives require no large buffer.
#include <stdafx.h>
#include <vd2/system/zip.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<uint8>;
void LE(Bytes& b, uint64 value, unsigned width) {
	for (unsigned i = 0; i < width; ++i) { b.push_back((uint8)value); value >>= 8; }
}
void Set(Bytes& b, size_t offset, uint64 value, unsigned width) {
	for (unsigned i = 0; i < width; ++i) { b.at(offset + i) = (uint8)value; value >>= 8; }
}
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
struct Sparse final : IVDRandomAccessStream {
	uint64 size = 0, position = 0, bytesRead = 0;
	std::vector<std::pair<uint64, Bytes>> segments;
	const wchar_t* GetNameForError() override { return L"sparse ZIP64 fixture"; }
	sint64 Pos() override { return (sint64)position; }
	sint64 Length() override { return (sint64)size; }
	void Seek(sint64 pos) override {
		if (pos < 0 || (uint64)pos > size) throw MyError("Invalid sparse seek");
		position = (uint64)pos;
	}
	void Write(const void*, sint32) override { throw MyError("Read-only fixture"); }
	void Read(void* data, sint32 count) override {
		if (ReadData(data, count) != count) throw MyError("Truncated fixture");
	}
	sint32 ReadData(void* data, sint32 count) override {
		if (count < 0) throw MyError("Negative fixture read");
		const uint64 n = std::min<uint64>(count, size - position);
		memset(data, 0, (size_t)n);
		for (const auto& segment : segments) {
			const uint64 begin = std::max(position, segment.first);
			const uint64 end = std::min(position + n, segment.first + segment.second.size());
			if (begin < end) memcpy((uint8*)data + (size_t)(begin - position),
				segment.second.data() + (size_t)(begin - segment.first), (size_t)(end - begin));
		}
		position += n; bytesRead += n;
		return (sint32)n;
	}
};
Sparse Archive(uint64 localOffset, unsigned count = 1) {
	Sparse stream;
	Bytes local(30, 0);
	Set(local, 0, 0x04034B50, 4); Set(local, 4, 45, 2);
	Set(local, 18, 1, 4); Set(local, 22, 1, 4); Set(local, 26, 8, 2);
	const char name[] = "game.xex";
	local.insert(local.end(), name, name + 8); local.push_back(0xEA);
	VDCRCChecker crc(VDCRCTable::CRC32); crc.Init(); crc.Process(local.data() + 38, 1);
	Set(local, 14, crc.CRC(), 4);
	stream.segments.push_back({localOffset, local});
	Bytes directory;
	for (unsigned i = 0; i < count; ++i) {
		Bytes entry(46, 0);
		Set(entry, 0, 0x02014B50, 4); Set(entry, 6, 45, 2);
		Set(entry, 16, crc.CRC(), 4); Set(entry, 20, 1, 4); Set(entry, 24, 1, 4);
		Set(entry, 28, 8, 2); Set(entry, 30, 12, 2); Set(entry, 42, 0xFFFFFFFF, 4);
		entry.insert(entry.end(), name, name + 8);
		LE(entry, 1, 2); LE(entry, 8, 2); LE(entry, localOffset, 8);
		directory.insert(directory.end(), entry.begin(), entry.end());
	}
	const uint64 directoryOffset = localOffset + local.size();
	const uint64 zip64Offset = directoryOffset + directory.size();
	Bytes end;
	LE(end, 0x06064B50, 4); LE(end, 44, 8); LE(end, 45, 2); LE(end, 45, 2);
	LE(end, 0, 4); LE(end, 0, 4); LE(end, count, 8); LE(end, count, 8);
	LE(end, directory.size(), 8); LE(end, directoryOffset, 8);
	LE(end, 0x07064B50, 4); LE(end, 0, 4); LE(end, zip64Offset, 8); LE(end, 1, 4);
	LE(end, 0x06054B50, 4); LE(end, 0, 2); LE(end, 0, 2);
	LE(end, 0xFFFF, 2); LE(end, 0xFFFF, 2);
	LE(end, 0xFFFFFFFF, 4); LE(end, 0xFFFFFFFF, 4); LE(end, 0, 2);
	directory.insert(directory.end(), end.begin(), end.end());
	stream.size = directoryOffset + directory.size();
	stream.segments.push_back({directoryOffset, std::move(directory)});
	return stream;
}
void Rejected(Sparse stream, const char* why) {
	bool rejected = false;
	try { VDZipArchive zip; zip.Init(&stream); } catch (const MyError&) { rejected = true; }
	Check(rejected, why);
}
}
int main() {
	try {
		auto large = Archive((1ULL << 32) + 4096);
		VDZipArchive zip; zip.Init(&large);
		Check(zip.GetFileCount() == 1 && zip.GetFileInfo(0).mUncompressedSize == 1,
			"ZIP64 small member metadata lost");
		vdautoptr<IVDInflateStream> file(zip.OpenDecodedStream(0));
		uint8 value = 0; file->Read(&value, 1); uint8 extra;
		Check(value == 0xEA && file->ReadData(&extra, 1) == 0, "ZIP64 offset truncated above 4GiB");
		file->VerifyCRC();
		Check(large.bytesRead < 128 * 1024, "Sparse ZIP64 index read scales with archive size");
		auto many = Archive(0, 65536); zip.Init(&many);
		Check(zip.GetFileCount() == 65536, "ZIP64 entry count truncated to 16 bits");
		auto malformed = Archive(0);
		Set(malformed.segments.back().second, 46 + 8 + 2, 7, 2);
		Rejected(std::move(malformed), "Truncated ZIP64 extra accepted");
		auto locator = Archive(0);
		Set(locator.segments.back().second, locator.segments.back().second.size() - 22 - 12, UINT64_MAX, 8);
		Rejected(std::move(locator), "Invalid ZIP64 locator offset accepted");
		auto oversized = Archive(0);
		const size_t record = oversized.segments.back().second.size() - 98;
		Set(oversized.segments.back().second, record + 24, 250001, 8);
		Set(oversized.segments.back().second, record + 32, 250001, 8);
		Rejected(std::move(oversized), "Unbounded ZIP64 index count accepted");
		auto outOfRange = Archive(0);
		Set(outOfRange.segments.back().second, 46 + 8 + 4, UINT64_MAX, 8);
		Rejected(std::move(outOfRange), "Out-of-archive ZIP64 local header accepted");
		std::puts("ZIP64 >4GiB sparse offsets, 65536 entries and malformed-index tests passed");
	} catch (const MyError& error) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
	catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
