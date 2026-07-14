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
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/enumparseimpl.h>
#include "oshelper.h"
#include "resource.h"

#include "../romdata/kernel_rom.h"
#include "../romdata/kernelxl_rom.h"
#include "../romdata/nokernel_rom.h"
#include "../romdata/atbasic_rom.h"
#include "../romdata/audio_samples.h"
#include "../romdata/romset_readme.h"
#include "../romdata/dbghelp.h"

// ---- ATProcessEfficiencyMode enum table ----
// Required by options.cpp (ATOptionsExchangeEnum). The full GUI build
// defines this in oshelper_sdl3.cpp; the headless bridge needs it here.
AT_DEFINE_ENUM_TABLE_BEGIN(ATProcessEfficiencyMode)
	{ ATProcessEfficiencyMode::Default, "default" },
	{ ATProcessEfficiencyMode::Performance, "performance" },
	{ ATProcessEfficiencyMode::Efficiency, "efficiency" },
AT_DEFINE_ENUM_TABLE_END(ATProcessEfficiencyMode, ATProcessEfficiencyMode::Default)

// ---- Embedded resources (kernel ROMs, BASIC, misc data) ----
// Keep this table in sync with source/os/oshelper_sdl3.cpp. The
// headless bridge has no SDL dependency, but it must still expose the
// same built-in AltirraOS/BASIC resources as AltirraSDL; otherwise a
// fresh standalone bridge silently boots with NoKernel until the user
// happens to configure external ROM paths via the GUI.

namespace {
	enum class EmbeddedKind : uint8 {
		Kernel,
		Stuff,
	};

	struct EmbeddedResource {
		int              id;
		EmbeddedKind     kind;
		const uint8     *data;
		uint32           size;
	};

	const EmbeddedResource kEmbeddedResources[] = {
		{ IDR_KERNEL,   EmbeddedKind::Kernel, kernel_rom,   kernel_rom_len   },
		{ IDR_KERNELXL, EmbeddedKind::Kernel, kernelxl_rom, kernelxl_rom_len },
		{ IDR_NOKERNEL, EmbeddedKind::Kernel, nokernel_rom, nokernel_rom_len },
		{ IDR_BASIC,    EmbeddedKind::Kernel, atbasic_rom,  atbasic_rom_len  },

		{ IDR_DISK_SPIN,            EmbeddedKind::Stuff, audio_disk_spin,            audio_disk_spin_len            },
		{ IDR_TRACK_STEP,           EmbeddedKind::Stuff, audio_track_step,           audio_track_step_len           },
		{ IDR_TRACK_STEP_2,         EmbeddedKind::Stuff, audio_track_step_2,         audio_track_step_2_len         },
		{ IDR_TRACK_STEP_3,         EmbeddedKind::Stuff, audio_track_step_3,         audio_track_step_3_len         },
		{ IDR_SPEAKER_STEP,         EmbeddedKind::Stuff, audio_speaker_click,        audio_speaker_click_len        },
		{ IDR_1030RELAY,            EmbeddedKind::Stuff, audio_1030relay,            audio_1030relay_len            },
		{ IDR_PRINTER_1029_PIN,     EmbeddedKind::Stuff, audio_printer_1029_pin,     audio_printer_1029_pin_len     },
		{ IDR_PRINTER_1029_PLATEN,  EmbeddedKind::Stuff, audio_printer_1029_platen,  audio_printer_1029_platen_len  },
		{ IDR_PRINTER_1029_RETRACT, EmbeddedKind::Stuff, audio_printer_1029_retract, audio_printer_1029_retract_len },
		{ IDR_PRINTER_1029_HOME,    EmbeddedKind::Stuff, audio_printer_1029_home,    audio_printer_1029_home_len    },
		{ IDR_PRINTER_1025_FEED,    EmbeddedKind::Stuff, audio_printer_1025_feed,    audio_printer_1025_feed_len    },
		{ IDR_PRINTER_1020_HEADMOVE,    EmbeddedKind::Stuff, audio_printer_1020_headmove,    audio_printer_1020_headmove_len    },
		{ IDR_PRINTER_1020_HEADREVERSE, EmbeddedKind::Stuff, audio_printer_1020_headreverse, audio_printer_1020_headreverse_len },
		{ IDR_PRINTER_1020_PAPERFEED,   EmbeddedKind::Stuff, audio_printer_1020_paperfeed,   audio_printer_1020_paperfeed_len   },
		{ IDR_PRINTER_1020_PENDOWN,     EmbeddedKind::Stuff, audio_printer_1020_pendown,     audio_printer_1020_pendown_len     },
		{ IDR_PRINTER_1020_PENUP,       EmbeddedKind::Stuff, audio_printer_1020_penup,       audio_printer_1020_penup_len       },
		{ IDR_ROMSETREADME,         EmbeddedKind::Stuff, romset_readme,              romset_readme_len              },
		{ IDR_DEBUG_HELP,           EmbeddedKind::Stuff, dbghelp,                    dbghelp_len                    },
	};

	const EmbeddedResource *FindEmbeddedResource(int id, EmbeddedKind kind) {
		for (const auto& e : kEmbeddedResources) {
			if (e.id == id && e.kind == kind)
				return &e;
		}
		return nullptr;
	}
}

const void *ATLockResource(uint32 id, size_t& size) {
	const EmbeddedResource *r = FindEmbeddedResource((int)id, EmbeddedKind::Stuff);
	if (r) {
		size = r->size;
		return r->data;
	}
	size = 0;
	return nullptr;
}

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size, bool allowPartial) {
	const EmbeddedResource *r = FindEmbeddedResource(id, EmbeddedKind::Kernel);
	if (!r)
		return false;

	if (offset > r->size)
		return false;

	uint32 avail = r->size - offset;
	if (avail < size) {
		if (!allowPartial)
			return false;
		size = avail;
	}

	memcpy(dst, r->data + offset, size);
	return true;
}

bool ATLoadKernelResource(int id, vdfastvector<uint8>& data) {
	const EmbeddedResource *r = FindEmbeddedResource(id, EmbeddedKind::Kernel);
	if (!r)
		return false;

	data.assign(r->data, r->data + r->size);
	return true;
}

bool ATLoadKernelResourceLZPacked(int id, vdfastvector<uint8>& data) {
	const EmbeddedResource *r = FindEmbeddedResource(id, EmbeddedKind::Kernel);
	if (!r || r->size < 4)
		return false;

	const uint8 *packed = r->data;
	const uint32 len = VDReadUnalignedLEU32(packed);

	data.clear();
	data.resize(len);

	uint8 *dst = data.data();
	const uint8 *src = packed + 4;

	for(;;) {
		uint8 c = *src++;

		if (!c)
			break;

		if (c & 1) {
			int distm1 = *src++;
			int runlen;

			if (c & 2) {
				distm1 += (c & 0xfc) << 6;
				runlen = *src++;
			} else {
				distm1 += ((c & 0x1c) << 6);
				runlen = c >> 5;
			}

			runlen += 3;

			const uint8 *csrc = dst - distm1 - 1;

			do {
				*dst++ = *csrc++;
			} while(--runlen);
		} else {
			c >>= 1;

			memcpy(dst, src, c);
			src += c;
			dst += c;
		}
	}

	return true;
}

bool ATLoadMiscResource(int id, vdfastvector<uint8>& data) {
	const EmbeddedResource *r = FindEmbeddedResource(id, EmbeddedKind::Stuff);
	if (!r)
		return false;

	data.assign(r->data, r->data + r->size);
	return true;
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
