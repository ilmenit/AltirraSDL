//	Altirra headless bridge server — oshelper.h stubs.
//
//	The headless server doesn't load images, copy frames to clipboard,
//	or manage windows. All real implementations live in
//	source/os/oshelper_sdl3.cpp for the GUI build. This file provides
//	minimal no-op / fail-safe stubs so the linker is satisfied.

#include <stdafx.h>

#include <cstdlib>
#include <cstring>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/error.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/enumparseimpl.h>
#include "oshelper.h"

// ---- ATProcessEfficiencyMode enum table ----
// Required by options.cpp (ATOptionsExchangeEnum). The full GUI build
// defines this in oshelper_sdl3.cpp; the headless bridge needs it here.
AT_DEFINE_ENUM_TABLE_BEGIN(ATProcessEfficiencyMode)
	{ ATProcessEfficiencyMode::Default, "default" },
	{ ATProcessEfficiencyMode::Performance, "performance" },
	{ ATProcessEfficiencyMode::Efficiency, "efficiency" },
AT_DEFINE_ENUM_TABLE_END(ATProcessEfficiencyMode, ATProcessEfficiencyMode::Default)

// ---- Embedded resource stubs (kernel ROMs, misc data) ----
// The headless server loads ROMs from disk via settings.ini paths.
// These functions are called by firmwaremanager.cpp for the built-in
// AltirraOS/BASIC but the headless build relies on external ROM files.

const void *ATLockResource(uint32 id, size_t& size) {
	size = 0;
	return nullptr;
}

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size, bool allowPartial) {
	return false;
}

bool ATLoadKernelResource(int id, vdfastvector<uint8>& data) {
	return false;
}

bool ATLoadKernelResourceLZPacked(int id, vdfastvector<uint8>& data) {
	return false;
}

bool ATLoadMiscResource(int id, vdfastvector<uint8>& data) {
	return false;
}

bool ATLoadImageResource(uint32 id, VDPixmapBuffer& buf) {
	return false;
}

// ---- Image load/save stubs ----

void ATLoadFrame(VDPixmapBuffer& /*px*/, const wchar_t * /*filename*/) {
	throw MyError("ATLoadFrame not available in headless mode.");
}

void ATLoadFrameFromMemory(VDPixmapBuffer& /*px*/, const void * /*mem*/, size_t /*len*/) {
	throw MyError("ATLoadFrameFromMemory not available in headless mode.");
}

// ---- File attribute stub ----

void ATFileSetReadOnlyAttribute(const wchar_t * /*path*/, bool /*readOnly*/) {
	// No-op in headless mode.
}

// ---- GUID generation ----

void ATGenerateGuid(uint8 guid[16]) {
	// Best-effort: /dev/urandom on POSIX, rand() fallback.
#if defined(__linux__) || defined(__APPLE__)
	FILE *f = fopen("/dev/urandom", "rb");
	if (f) {
		size_t n = fread(guid, 1, 16, f);
		fclose(f);
		if (n == 16) return;
	}
#endif
	// Fallback: not cryptographically secure but sufficient for
	// VHD image creation (the only caller).
	for (int i = 0; i < 16; ++i)
		guid[i] = (uint8)(std::rand() & 0xFF);
}
