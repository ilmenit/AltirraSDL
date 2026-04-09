// AltirraBridge — bare-metal boot stub implementation
//
// Emits the stub .xex from an embedded byte array to a cross-platform
// temp file, so BOOT_BARE can route through the normal XEX loader
// path without needing a separate in-memory load code path.
//
// The stub bytes below correspond to this 6502 source (kept as
// reference in sdk/assets/bare_metal.s):
//
//     * = $0600
//     start:  sei                  ; 78
//             cld                  ; d8
//             lda #$ff             ; a9 ff
//             sta $d301            ; 8d 01 d3  — disable BASIC cart
//             lda #$00             ; a9 00
//             sta $d40e            ; 8d 0e d4  — NMIEN = 0
//             sta $d400            ; 8d 00 d4  — DMACTL = 0
//     park:   jmp park             ; 4c 0f 06
//
// Total 18 bytes at $0600..$0611. Wrapped in the Atari DOS/XL XEX
// container: $FF $FF magic, one code segment, then a RUNAD segment
// pointing at $0600 so the OS loader jumps to start after loading.

#include <stdafx.h>

#include "bridge_bare_stub.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#  include <direct.h>
#  include <process.h>   // _getpid
#  define AT_BRIDGE_STUB_PATH_SEP '\\'
#else
#  include <unistd.h>    // getpid
#  include <sys/stat.h>
#  define AT_BRIDGE_STUB_PATH_SEP '/'
#endif

namespace ATBridge {

namespace {

// -------- Embedded XEX bytes ------------------------------------------------
// Keep in sync with sdk/assets/bare_metal.s. A compile-time size
// assert catches accidental edits.
const unsigned char kBareStubXex[] = {
    // XEX magic
    0xFF, 0xFF,
    // Segment header: load $0600..$0611
    0x00, 0x06, 0x11, 0x06,
    // Code at $0600 (18 bytes)
    0x78,                   //        sei
    0xD8,                   //        cld
    0xA9, 0xFF,             //        lda #$ff
    0x8D, 0x01, 0xD3,       //        sta $d301        ; disable BASIC
    0xA9, 0x00,             //        lda #$00
    0x8D, 0x0E, 0xD4,       //        sta $d40e        ; NMIEN = 0
    0x8D, 0x00, 0xD4,       //        sta $d400        ; DMACTL = 0
    0x4C, 0x0F, 0x06,       // park:  jmp park
    // RUNAD segment header (special $02E0/$02E1 range)
    0xE0, 0x02, 0xE1, 0x02,
    // RUNAD = $0600 — OS loader JSR/JMPs here after loading
    0x00, 0x06,
};
static_assert(sizeof(kBareStubXex) == 30,
              "bare_stub XEX has drifted — check assembly source");

// Resolve a writable temp directory. Prefers environment variables
// the user can redirect, then a platform-default fallback. The
// returned string does NOT have a trailing separator.
std::string GetTempDir() {
#if defined(_WIN32)
    // Windows: %TEMP%, %TMP%, %USERPROFILE%, or literal fallback.
    const char* env[] = { "TEMP", "TMP", "USERPROFILE", nullptr };
    for (int i = 0; env[i]; ++i) {
        const char* v = std::getenv(env[i]);
        if (v && *v) return std::string(v);
    }
    return std::string("C:\\Temp");
#else
    // POSIX: $TMPDIR, else /tmp.
    const char* v = std::getenv("TMPDIR");
    if (v && *v) {
        // Strip trailing slash if present
        std::string s(v);
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    }
    return std::string("/tmp");
#endif
}

// Format the per-process stub path. Using the pid in the filename
// keeps multiple concurrent bridge servers from clobbering each
// other's stub files.
std::string ComputeStubPath() {
#if defined(_WIN32)
    int pid = static_cast<int>(_getpid());
#else
    int pid = static_cast<int>(getpid());
#endif
    std::string dir = GetTempDir();
    char buf[64];
    std::snprintf(buf, sizeof buf,
                  "%caltirra-bridge-bare-%d.xex",
                  AT_BRIDGE_STUB_PATH_SEP, pid);
    return dir + buf;
}

// Write (or rewrite) the stub file. Called lazily under mutex.
// Returns true on success.
bool WriteStubFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t n = std::fwrite(kBareStubXex, 1, sizeof kBareStubXex, f);
    const bool ok = (n == sizeof kBareStubXex);
    std::fclose(f);
    return ok;
}

}  // namespace

std::string EnsureBareStubXexPath() {
    static std::mutex mtx;
    static std::string cachedPath;
    static bool cachedOk = false;

    std::lock_guard<std::mutex> lock(mtx);
    if (cachedOk) {
        // Re-write if the file was removed between calls (someone
        // cleaned out /tmp, or a test harness is reusing the
        // fixture). The write is cheap (~30 bytes) and guarantees
        // the file is present when the caller hands it to the
        // boot dispatcher.
        std::FILE* probe = std::fopen(cachedPath.c_str(), "rb");
        if (probe) {
            std::fclose(probe);
            return cachedPath;
        }
    }

    cachedPath = ComputeStubPath();
    if (!WriteStubFile(cachedPath)) {
        cachedPath.clear();
        cachedOk = false;
        return std::string();
    }
    cachedOk = true;
    return cachedPath;
}

}  // namespace ATBridge
