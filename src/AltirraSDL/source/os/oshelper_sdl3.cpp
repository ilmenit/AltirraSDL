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
//	Dependency budget: SDL3 + SDL3_image + Dear ImGui.  SDL3_image
//	handles PNG/JPEG load/save (replaces the previous stb_image vendored
//	dependency which had known security issues).
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
// SDL3_image — PNG/JPEG load/save
// ---------------------------------------------------------------------------
#include <SDL3_image/SDL_image.h>

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
	// Embedded PNG/JPEG images are loaded via the same SDL3_image decode
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
// Image load/save (SDL3_image)
// ===========================================================================
//
// SDL3_image loads images into SDL_Surface, which we convert to/from the
// VD pixmap format kPixFormat_XRGB8888 (bytes B,G,R,X on little-endian,
// matching GDI CF_DIB / WIC GUID_WICPixelFormat32bppBGRA).
//
// SDL_ConvertSurface handles the pixel format conversion from whatever
// the source image uses to our target BGRX layout, so no manual channel
// swaps are needed.

// Convert an SDL_Surface (any format) into a VDPixmapBuffer (XRGB8888).
static void ATSurfaceToPixmap(SDL_Surface *surf, VDPixmapBuffer& px) {
	// Convert to BGRA32 (B,G,R,A byte order on all architectures — the *32
	// aliases specify memory byte order portably) which matches
	// kPixFormat_XRGB8888 layout exactly (alpha channel ignored by consumers).
	SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
	if (!conv)
		throw MyError("SDL_ConvertSurface failed: %s", SDL_GetError());

	try {
		px.init(conv->w, conv->h, nsVDPixmap::kPixFormat_XRGB8888);
		for (int y = 0; y < conv->h; ++y) {
			const uint8 *src = (const uint8*)conv->pixels + (ptrdiff_t)conv->pitch * y;
			uint8 *dst = (uint8*)px.data + (ptrdiff_t)px.pitch * y;
			memcpy(dst, src, (size_t)conv->w * 4);
		}
	} catch(...) {
		SDL_DestroySurface(conv);
		throw;
	}
	SDL_DestroySurface(conv);
}

void ATLoadFrameFromMemory(VDPixmapBuffer& px, const void *mem, size_t len) {
	if (!mem || len == 0 || len > (size_t)INT32_MAX)
		throw MyError("Invalid image data.");

	SDL_IOStream *io = SDL_IOFromConstMem(mem, (size_t)len);
	if (!io)
		throw MyError("SDL_IOFromConstMem failed: %s", SDL_GetError());

	// IMG_Load_IO auto-detects format (PNG, JPEG, BMP, etc.).
	// closeio=true so the IOStream is freed even on error.
	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf)
		throw MyError("Unable to decode image: %s", SDL_GetError());

	try {
		ATSurfaceToPixmap(surf, px);
	} catch(...) {
		SDL_DestroySurface(surf);
		throw;
	}
	SDL_DestroySurface(surf);
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

	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf)
		throw MyError("Unable to decode %s: %s", u8.c_str(), SDL_GetError());

	try {
		ATSurfaceToPixmap(surf, px);
	} catch(...) {
		SDL_DestroySurface(surf);
		throw;
	}
	SDL_DestroySurface(surf);
}

void ATSaveFrame(const VDPixmap& px, const wchar_t *filename) {
	if (!filename)
		throw MyError("No filename.");

	// Convert to a contiguous 32-bit BGRX buffer (via VDPixmapBlt which
	// handles arbitrary input formats).
	VDPixmapBuffer bgrx(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);
	VDPixmapBlt(bgrx, px);

	// Create an SDL_Surface that views the BGRX pixel data (no copy).
	// BGRA32 = B,G,R,A byte order on all architectures = kPixFormat_XRGB8888.
	SDL_Surface *surf = SDL_CreateSurfaceFrom(
		bgrx.w, bgrx.h, SDL_PIXELFORMAT_BGRA32,
		bgrx.data, (int)bgrx.pitch);
	if (!surf)
		throw MyError("SDL_CreateSurfaceFrom failed: %s", SDL_GetError());

	VDStringA u8 = VDTextWToU8(VDStringW(filename));

	bool ok = IMG_SavePNG(surf, u8.c_str());
	SDL_DestroySurface(surf);

	if (!ok)
		throw MyError("Failed to save PNG %s: %s", u8.c_str(), SDL_GetError());
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
