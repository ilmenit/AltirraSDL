//	Altirra SDL3 frontend — portable oshelper.h implementations.
//
//	This file replaces the Windows-specific src/Altirra/source/oshelper.cpp
//	for the SDL3/CMake build and contains all non-UI platform-integration
//	helpers: embedded resource lookup, image load/save, GUID generation,
//	file attribute flips.
//
//	Supported platforms (same single file; gated by #if):
//	  - Windows (SDL3 CMake build; native .sln build still uses oshelper.cpp)
//	  - macOS 10.15+ (x64 + arm64 universal)
//	  - Linux (glibc 2.25+ for getentropy; /dev/urandom fallback below)
//	  - Android API 28+ (getentropy; /dev/urandom fallback below)
//
//	Dependency budget: SDL3 + Dear ImGui only. stb_image.h and
//	stb_image_write.h are vendored in src/AltirraSDL/vendor/stb/ and are
//	treated as vendored source, not an external dependency.
//
//	Threading contract:
//	  Functions that call into SDL3 (SDL_IOFromFile, SDL_GetBasePath, ...)
//	  are safe from any thread except where the SDL3 docs require the main
//	  thread. SDL_OpenURL, SDL_SetClipboardData, and SDL_SetClipboardText
//	  are main-thread only; the oshelper.h functions that wrap them
//	  (ATLaunchURL, ATCopyTextToClipboard, ATCopyFrameToClipboard,
//	  ATShowHelp, ATShowFileInSystemExplorer) must be invoked on the main
//	  thread. Callers dispatched from worker threads must route through
//	  IATAsyncDispatcher::Queue first.

#include <stdafx.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

// getentropy() availability by platform:
//   macOS 10.12+  — <sys/random.h> always present
//   glibc 2.25+   — <sys/random.h> always present
//   Android       — bionic exposes getentropy() only at API 28+, but the
//                   header <sys/random.h> is always present on NDK r15+.
//                   We include unconditionally and gate the *call* at the
//                   use site below.
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#  include <sys/random.h>
#endif

#include <SDL3/SDL.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/text.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/pixmapops.h>
#include <at/atcore/enumparseimpl.h>

#include "oshelper.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// stb_image / stb_image_write — single-translation-unit implementation
// ---------------------------------------------------------------------------
//
// Both headers are header-only single files; exactly one .cpp per
// translation unit must define the _IMPLEMENTATION macro. This file is
// that TU. No external stb dependency is linked.
//
// We disable the PSD/GIF/PIC/PNM decoders we don't need — Altirra only
// loads PNG and JPEG images (palettes, reference frames, artifacting
// references). Shrinks the compiled size.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STB_IMAGE_STATIC
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

// Embedded ROM data (built from src/Kernel/ and src/ATBasic/ via MADS).
// The romdata directory lives at src/AltirraSDL/romdata/; this file is at
// src/AltirraSDL/source/os/, so the relative prefix is ../../romdata/.
#include "../../romdata/kernel_rom.h"
#include "../../romdata/kernelxl_rom.h"
#include "../../romdata/nokernel_rom.h"

// Embedded PCM audio samples for stock sounds
#include "../../romdata/audio_samples.h"

// Embedded ROM set readme (romset.html) for Export ROM Set
#include "../../romdata/romset_readme.h"

// Embedded debugger help text (dbghelp.txt) for .help command
#include "../../romdata/dbghelp.h"

AT_DEFINE_ENUM_TABLE_BEGIN(ATProcessEfficiencyMode)
	{ ATProcessEfficiencyMode::Default, "default" },
	{ ATProcessEfficiencyMode::Performance, "performance" },
	{ ATProcessEfficiencyMode::Efficiency, "efficiency" },
AT_DEFINE_ENUM_TABLE_END(ATProcessEfficiencyMode, ATProcessEfficiencyMode::Default)

// ===========================================================================
// Embedded resource table — single source of truth
// ===========================================================================
//
// Windows Altirra stores these as PE resources of type KERNEL / STUFF (see
// src/Altirra/res/Altirra.rc). The SDL3 build compiles the same payload
// bytes into the binary via generated C headers (see src/AltirraSDL/romdata/).
//
// This single table is consulted by ATLockResource, ATLoadKernelResource
// (both overloads), and ATLoadMiscResource. Adding a new embedded blob
// requires exactly one new row here; there is no separate whitelist to
// keep in sync.
//
// Ordering is irrelevant (linear scan); the "kind" column documents which
// Windows resource type the row mirrors so that any drift against the
// Altirra.rc can be audited easily.

namespace {
	enum class EmbeddedKind : uint8 {
		Kernel,     // mirrors Windows "KERNEL" resource type
		Stuff,      // mirrors Windows "STUFF" resource type
	};

	struct EmbeddedResource {
		int              id;
		EmbeddedKind     kind;
		const uint8     *data;
		uint32           size;
	};

	const EmbeddedResource kEmbeddedResources[] = {
		// --- KERNEL ROMs ----------------------------------------------------
		{ IDR_KERNEL,   EmbeddedKind::Kernel, kernel_rom,   kernel_rom_len   },
		{ IDR_KERNELXL, EmbeddedKind::Kernel, kernelxl_rom, kernelxl_rom_len },
		{ IDR_NOKERNEL, EmbeddedKind::Kernel, nokernel_rom, nokernel_rom_len },

		// --- STUFF resources (audio samples, HTML/text blobs) --------------
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

	const EmbeddedResource *FindEmbeddedResourceAnyKind(int id) {
		for (const auto& e : kEmbeddedResources) {
			if (e.id == id)
				return &e;
		}
		return nullptr;
	}
}

// ===========================================================================
// Base path helper (SDL3)
// ===========================================================================
//
// SDL_GetBasePath() is:
//   - Windows/Linux desktop: exe directory
//   - macOS: .app/Contents/Resources/ (default; do NOT override via the
//     SDL_FILESYSTEM_BASE_DIR_TYPE Info.plist key or resource lookup breaks)
//   - Android: "./" — and SDL_IOFromFile then transparently resolves
//     relative paths against the APK asset manager
//
// SDL docs say the first call may be slow, so we cache.

static const VDStringA& ATGetSDLBasePath() {
	static VDStringA sBasePath;
	static bool sInitialized = false;
	if (!sInitialized) {
		const char *p = SDL_GetBasePath();
		if (p) sBasePath = p;
		sInitialized = true;
	}
	return sBasePath;
}

// ===========================================================================
// Resource loaders (embedded blobs)
// ===========================================================================

const void *ATLockResource(uint32 id, size_t& size) {
	// Parity with Windows oshelper.cpp: ATLockResource looks up the
	// "STUFF" resource type only. Passing a KERNEL id here returns NULL
	// on Windows (different resource type) and must do the same here so
	// callers that test the result don't silently pick up a kernel blob.
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

bool ATLoadKernelResource(int id, vdfastvector<uint8>& buf) {
	const EmbeddedResource *r = FindEmbeddedResource(id, EmbeddedKind::Kernel);
	if (!r)
		return false;

	buf.assign(r->data, r->data + r->size);
	return true;
}

// Port of the LZ decoder from src/Altirra/source/oshelper.cpp (Windows build).
// Unchanged arithmetic; no platform code.
bool ATLoadKernelResourceLZPacked(int id, vdfastvector<uint8>& data) {
	const EmbeddedResource *r = FindEmbeddedResource(id, EmbeddedKind::Kernel);
	if (!r || r->size < 4)
		return false;

	const uint8 *packed = r->data;

	uint32 len = VDReadUnalignedLEU32(packed);

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
	// Embedded PNG/JPEG images are loaded via the same stb_image decode
	// path as runtime file loads. Today no embedded image resources are
	// registered in kEmbeddedResources (the Windows build stores them as
	// PNG resources in the .rc); adding one is a one-line table entry
	// plus the binary embed. We accept any kind on lookup so a future
	// dedicated EmbeddedKind::Image row would Just Work, and so that
	// legacy callers that reuse a STUFF id for an image payload work.
	const EmbeddedResource *r = FindEmbeddedResourceAnyKind((int)id);
	if (!r)
		return false;

	try {
		ATLoadFrameFromMemory(buf, r->data, r->size);
		return true;
	} catch(const MyError&) {
		return false;
	}
}

// ===========================================================================
// Image load/save (stb_image)
// ===========================================================================
//
// Byte-order pitfall: stb_image returns R,G,B,A byte order. The VD pixmap
// format kPixFormat_XRGB8888 stores bytes as B,G,R,X on little-endian
// (matches GDI CF_DIB / WIC GUID_WICPixelFormat32bppBGRA). The channel
// swap below is mandatory — without it every loaded image is color-
// inverted.

static void ATSwapRGBAtoBGRX(const uint8 *rgba, VDPixmapBuffer& px) {
	const sint32 w = px.w;
	const sint32 h = px.h;
	for (sint32 y = 0; y < h; ++y) {
		uint8 *dst = (uint8*)px.data + (sint64)px.pitch * y;
		const uint8 *src = rgba + (size_t)y * w * 4;
		for (sint32 x = 0; x < w; ++x) {
			dst[0] = src[2];  // B
			dst[1] = src[1];  // G
			dst[2] = src[0];  // R
			dst[3] = 0xFF;    // X (alpha ignored by kPixFormat_XRGB8888 consumers)
			src += 4;
			dst += 4;
		}
	}
}

void ATLoadFrameFromMemory(VDPixmapBuffer& px, const void *mem, size_t len) {
	if (!mem || len == 0 || len > (size_t)INT32_MAX)
		throw MyError("Invalid image data.");

	int w = 0, h = 0, comp = 0;
	unsigned char *rgba = stbi_load_from_memory(
		(const stbi_uc*)mem, (int)len, &w, &h, &comp, 4);
	if (!rgba) {
		const char *why = stbi_failure_reason();
		throw MyError("Unable to decode image: %s", why ? why : "unknown");
	}

	try {
		px.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);
		ATSwapRGBAtoBGRX(rgba, px);
	} catch(...) {
		stbi_image_free(rgba);
		throw;
	}
	stbi_image_free(rgba);
}

void ATLoadFrame(VDPixmapBuffer& px, const wchar_t *filename) {
	if (!filename)
		throw MyError("No filename.");

	VDStringA u8 = VDTextWToU8(VDStringW(filename));

	// SDL_IOFromFile handles:
	//  - UTF-8 paths on all platforms (converts to wide on Windows)
	//  - content:// URIs on Android
	//  - APK asset fallback on Android for relative paths
	SDL_IOStream *io = SDL_IOFromFile(u8.c_str(), "rb");
	if (!io)
		throw MyError("Cannot open %s: %s", u8.c_str(), SDL_GetError());

	Sint64 sz = SDL_GetIOSize(io);
	if (sz < 0 || sz > 256 * 1024 * 1024) {
		SDL_CloseIO(io);
		throw MyError("File too large or unknown size.");
	}

	vdblock<uint8> buf((size_t)sz);
	size_t total = 0;
	while (total < (size_t)sz) {
		size_t rd = SDL_ReadIO(io, buf.data() + total, (size_t)sz - total);
		if (rd == 0) {
			SDL_CloseIO(io);
			throw MyError("Short read on %s", u8.c_str());
		}
		total += rd;
	}
	SDL_CloseIO(io);

	ATLoadFrameFromMemory(px, buf.data(), (size_t)sz);
}

// stb_image_write sink: forwards bytes to an SDL_IOStream.
// Must keep a rolling failure flag because stbi_write_png_to_func cannot
// report mid-write errors through the callback.
namespace {
	struct StbWriteSink {
		SDL_IOStream *io;
		bool          failed;
	};

	void StbWriteSinkCb(void *context, void *data, int size) {
		auto *s = (StbWriteSink *)context;
		if (s->failed || size <= 0) return;
		size_t w = SDL_WriteIO(s->io, data, (size_t)size);
		if (w != (size_t)size)
			s->failed = true;
	}
}

void ATSaveFrame(const VDPixmap& px, const wchar_t *filename) {
	if (!filename)
		throw MyError("No filename.");

	// Convert to a contiguous 32-bit BGRX buffer (via VDPixmapBlt which
	// handles arbitrary input formats), then swap BGRX → RGBA for stb.
	VDPixmapBuffer bgrx(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);
	VDPixmapBlt(bgrx, px);

	const sint32 w = bgrx.w;
	const sint32 h = bgrx.h;
	vdblock<uint8> rgba((size_t)w * h * 4);
	for (sint32 y = 0; y < h; ++y) {
		const uint8 *src = (const uint8*)bgrx.data + (sint64)bgrx.pitch * y;
		uint8 *dst = rgba.data() + (size_t)y * w * 4;
		for (sint32 x = 0; x < w; ++x) {
			dst[0] = src[2];  // R ← B
			dst[1] = src[1];  // G
			dst[2] = src[0];  // B ← R
			dst[3] = 0xFF;    // A
			src += 4;
			dst += 4;
		}
	}

	VDStringA u8 = VDTextWToU8(VDStringW(filename));
	SDL_IOStream *io = SDL_IOFromFile(u8.c_str(), "wb");
	if (!io)
		throw MyError("Cannot open %s for writing: %s", u8.c_str(), SDL_GetError());

	StbWriteSink sink{ io, false };
	int ok = stbi_write_png_to_func(
		&StbWriteSinkCb, &sink,
		w, h, 4, rgba.data(), w * 4);

	SDL_CloseIO(io);

	if (!ok || sink.failed)
		throw MyError("Failed to encode/write PNG to %s", u8.c_str());
}

// ===========================================================================
// File attribute helpers
// ===========================================================================

void ATFileSetReadOnlyAttribute(const wchar_t *path, bool readOnly) {
	if (!path || !*path)
		throw MyError("No path.");

	VDStringA u8 = VDTextWToU8(VDStringW(path));

#if defined(_WIN32)
	// Windows SDL3 build path: use SetFileAttributesW so the behavior
	// matches the native .sln build. Convert UTF-8 back to wide for Win32.
	VDStringW wpath(path);
	DWORD attrs = GetFileAttributesW(wpath.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES)
		throw MyError("Unable to read attributes of %s: error %u",
			u8.c_str(), (unsigned)GetLastError());
	if (readOnly)
		attrs |= FILE_ATTRIBUTE_READONLY;
	else
		attrs &= ~FILE_ATTRIBUTE_READONLY;
	if (!SetFileAttributesW(wpath.c_str(), attrs))
		throw MyError("Unable to change read-only flag on %s: error %u",
			u8.c_str(), (unsigned)GetLastError());
#else
#  if defined(__ANDROID__)
	// Android: SAF paths (content://) have no POSIX mode bits. Silently
	// chmod'ing the fd's underlying tmpfile would succeed and lie to the
	// caller. Reject explicitly.
	if (u8.size() >= 10 && memcmp(u8.c_str(), "content://", 10) == 0)
		throw MyError("Read-only attribute is not supported on SAF paths (%s).",
			u8.c_str());
#  endif
	struct stat st;
	if (stat(u8.c_str(), &st) != 0)
		throw MyError("Unable to stat %s: %s", u8.c_str(), strerror(errno));
	mode_t mode = st.st_mode;
	if (readOnly)
		mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
	else
		mode |= S_IWUSR;
	if (chmod(u8.c_str(), mode) != 0)
		throw MyError("Unable to change read-only flag on %s: %s",
			u8.c_str(), strerror(errno));
#endif
}

// ===========================================================================
// GUID generation — cryptographic entropy on every platform
// ===========================================================================
//
// Previous /dev/urandom + rand() fallback produced duplicate GUIDs within a
// session when rand() was seeded from time(0). Replace with real CSPRNGs:
//   Windows:     BCryptGenRandom
//   macOS:       getentropy (10.12+)
//   Linux:       getentropy (glibc 2.25+), fall back to /dev/urandom
//   Android:     getentropy (API 28+), fall back to /dev/urandom

#if !defined(_WIN32)
namespace {
	bool ATReadDevUrandom(uint8 *out, size_t len) {
		int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return false;
		size_t got = 0;
		while (got < len) {
			ssize_t r = read(fd, out + got, len - got);
			if (r <= 0) {
				if (r < 0 && errno == EINTR) continue;
				close(fd);
				return false;
			}
			got += (size_t)r;
		}
		close(fd);
		return true;
	}
}
#endif

void ATGenerateGuid(uint8 rawguid[16]) {
	bool ok = false;

#if defined(_WIN32)
	if (BCryptGenRandom(nullptr, rawguid, 16,
			BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
		ok = true;
	}
#elif defined(__APPLE__)
	if (getentropy(rawguid, 16) == 0)
		ok = true;
#elif defined(__ANDROID__)
#  if __ANDROID_API__ >= 28
	if (getentropy(rawguid, 16) == 0)
		ok = true;
#  endif
	if (!ok)
		ok = ATReadDevUrandom(rawguid, 16);
#elif defined(__linux__)
	// getentropy() exists in glibc >= 2.25. Use it when available, else
	// /dev/urandom. No rand() fallback — that path produced duplicates.
#  if defined(__GLIBC_PREREQ)
#    if __GLIBC_PREREQ(2, 25)
	if (getentropy(rawguid, 16) == 0)
		ok = true;
#    endif
#  endif
	if (!ok)
		ok = ATReadDevUrandom(rawguid, 16);
#else
	ok = ATReadDevUrandom(rawguid, 16);
#endif

	if (!ok) {
		// Last-ditch: refuse to produce a predictable GUID. Throw so the
		// caller sees a hard failure instead of silently shipping a bogus
		// identifier. Callers already handle disk I/O errors similarly.
		throw MyError("System entropy source unavailable; cannot generate GUID.");
	}

	// RFC 4122 version 4 (random) + variant 1 (RFC 4122)
	rawguid[6] = (rawguid[6] & 0x0F) | 0x40;
	rawguid[8] = (rawguid[8] & 0x3F) | 0x80;
}
