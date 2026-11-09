#include <stdafx.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <at/atcore/memoryutils.h>
#include <vd2/system/file.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/zip.h>
#include "disk.h"
#include "diskprofile.h"

namespace {
	void Check(bool value, const char *message) {
		if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
	}

	sint64 CompressedSize(const std::vector<uint8>& data) {
		VDMemoryBufferStream output;
		VDDeflateStream compressor(output);
		compressor.Write(data.data(), (sint32)data.size());
		compressor.Finalize();
		return output.Pos();
	}
}

void ATTest28CoreRegressions() {
	ATRandomizeMemoryCompressible(nullptr, 0, 1);
	for (uint32 seed : {1U, 0x12345678U, 0x80000001U}) {
		for (size_t length : {0U, 1U, 31U, 32U, 33U, 1056U, 32769U}) {
			std::vector<uint8> a(length + 2, 0xA5), b(length + 2, 0xA5);
			ATRandomizeMemoryCompressible(a.data() + 1, length, seed);
			ATRandomizeMemoryCompressible(b.data() + 1, length, seed);
			Check(a == b, "Static RAM randomization is not deterministic");
			Check(a.front() == 0xA5 && a.back() == 0xA5,
				"Static RAM randomization wrote beyond requested range");
		}
	}
	std::vector<uint8> compressible(65536), noise(65536), other(65536);
	ATRandomizeMemoryCompressible(compressible.data(), compressible.size(), 1);
	ATRandomizeMemoryCompressible(other.data(), other.size(), 2);
	ATRandomizeMemory(noise.data(), noise.size(), 1);
	Check(compressible != other, "Static RAM randomization ignored seed");
	Check(CompressedSize(compressible) < CompressedSize(noise) / 2,
		"New static RAM pattern did not improve save-state compression");

	const auto& generic = ATGetDiskProfile(kATDiskEmulationMode_Generic);
	Check(generic.mbSupportedCmdHighSpeedXF
		&& generic.mCyclesPerSIOByteHighSpeedXF > 0
		&& generic.mCyclesPerSIOByteHighSpeedXF < generic.mCyclesPerSIOByte,
		"Generic disk profile has invalid XF551 high-speed timing");

	// Deleting an unopened leaf must remove it from the ordered key list
	// before freeing the map entry that owns it. Exercise allocator reuse
	// and enumeration to expose dangling list entries under sanitizers.
	VDRegistryProviderMemory registry;
	void *parent = registry.CreateKey(registry.GetUserKey(), "Test28", true);
	for (int i = 0; i < 256; ++i) {
		void *child = registry.CreateKey(parent, "leaf", true);
		registry.SetInt(child, "value", i);
		registry.CloseKey(child);
		Check(registry.RemoveKey(parent, "leaf"), "Registry leaf deletion failed");
		void *enumerator = registry.EnumKeysBegin(parent);
		Check(!registry.EnumKeysNext(enumerator), "Deleted registry key remained enumerable");
		registry.EnumKeysClose(enumerator);
	}
	registry.CloseKey(parent);
}
