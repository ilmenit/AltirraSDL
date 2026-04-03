//	Altirra SDL3 frontend - oshelper.h stub implementations
//	On Linux, built-in AltirraOS kernel ROMs are embedded as C arrays.
//	ATLoadKernelResource loads from these embedded blobs instead of
//	Windows resources.

#include <stdafx.h>
#include <string.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "oshelper.h"
#include "resource.h"

// Embedded ROM data (built from src/Kernel/ and src/ATBasic/ via MADS)
#include "../romdata/kernel_rom.h"
#include "../romdata/kernelxl_rom.h"
#include "../romdata/nokernel_rom.h"

// Embedded PCM audio samples for stock sounds (disk, speaker, printer, etc.)
#include "../romdata/audio_samples.h"

// Embedded ROM set readme (romset.html) for Export ROM Set
#include "../romdata/romset_readme.h"

// Embedded debugger help text (dbghelp.txt) for .help command
#include "../romdata/dbghelp.h"

struct EmbeddedROM {
	int resourceId;
	const unsigned char *data;
	unsigned int size;
};

static const EmbeddedROM kEmbeddedROMs[] = {
	{ IDR_KERNEL,   kernel_rom,   kernel_rom_len },
	{ IDR_KERNELXL, kernelxl_rom, kernelxl_rom_len },
	{ IDR_NOKERNEL, nokernel_rom, nokernel_rom_len },
};

static const EmbeddedROM kEmbeddedAudioSamples[] = {
	{ IDR_DISK_SPIN,            audio_disk_spin,            audio_disk_spin_len },
	{ IDR_TRACK_STEP,           audio_track_step,           audio_track_step_len },
	{ IDR_TRACK_STEP_2,         audio_track_step_2,         audio_track_step_2_len },
	{ IDR_TRACK_STEP_3,         audio_track_step_3,         audio_track_step_3_len },
	{ IDR_SPEAKER_STEP,         audio_speaker_click,        audio_speaker_click_len },
	{ IDR_1030RELAY,            audio_1030relay,            audio_1030relay_len },
	{ IDR_PRINTER_1029_PIN,     audio_printer_1029_pin,     audio_printer_1029_pin_len },
	{ IDR_PRINTER_1029_PLATEN,  audio_printer_1029_platen,  audio_printer_1029_platen_len },
	{ IDR_PRINTER_1029_RETRACT, audio_printer_1029_retract, audio_printer_1029_retract_len },
	{ IDR_PRINTER_1029_HOME,    audio_printer_1029_home,    audio_printer_1029_home_len },
	{ IDR_PRINTER_1025_FEED,    audio_printer_1025_feed,    audio_printer_1025_feed_len },
};

static const EmbeddedROM *FindROM(int resId) {
	for (const auto& rom : kEmbeddedROMs) {
		if (rom.resourceId == resId)
			return &rom;
	}
	return nullptr;
}

const void *ATLockResource(uint32 resId, size_t& size) {
	const EmbeddedROM *rom = FindROM(resId);
	if (rom) {
		size = rom->size;
		return rom->data;
	}
	size = 0;
	return nullptr;
}

bool ATLoadKernelResource(int resId, void *dst, uint32 offset, uint32 len, bool) {
	const EmbeddedROM *rom = FindROM(resId);
	if (!rom)
		return false;

	if (offset >= rom->size)
		return false;

	uint32 avail = rom->size - offset;
	uint32 toCopy = len < avail ? len : avail;
	memcpy(dst, rom->data + offset, toCopy);

	// Zero-fill remainder if requested more than available
	if (toCopy < len)
		memset((char *)dst + toCopy, 0, len - toCopy);

	return true;
}

bool ATLoadKernelResource(int resId, vdfastvector<uint8>& buf) {
	const EmbeddedROM *rom = FindROM(resId);
	if (!rom)
		return false;

	buf.resize(rom->size);
	memcpy(buf.data(), rom->data, rom->size);
	return true;
}

bool ATLoadKernelResourceLZPacked(int, vdfastvector<uint8>&) {
	return false;
}

bool ATLoadMiscResource(int id, vdfastvector<uint8>& buf) {
	// ROM set readme
	if (id == IDR_ROMSETREADME) {
		buf.resize(romset_readme_len);
		memcpy(buf.data(), romset_readme, romset_readme_len);
		return true;
	}

	// Debugger help text (.help command)
	if (id == IDR_DEBUG_HELP) {
		buf.resize(dbghelp_len);
		memcpy(buf.data(), dbghelp, dbghelp_len);
		return true;
	}

	for (const auto& sample : kEmbeddedAudioSamples) {
		if (sample.resourceId == id) {
			buf.resize(sample.size);
			memcpy(buf.data(), sample.data, sample.size);
			return true;
		}
	}
	return false;
}

bool ATLoadImageResource(uint32, VDPixmapBuffer&) {
	return false;
}

void ATFileSetReadOnlyAttribute(const wchar_t*, bool) {}
