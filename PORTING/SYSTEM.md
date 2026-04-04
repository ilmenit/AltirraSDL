# System Library Porting

The VirtualDub-heritage system library (`src/system/`, headers in
`src/h/vd2/system/`) provides threading, file I/O, filesystem operations,
atomics, and configuration storage. It is used by virtually every module in
the project (~935 source files include its headers). All current
implementations use Win32 APIs exclusively.

The porting strategy is **conditional compilation**: headers gain minimal
`#ifdef` blocks where platform-specific types appear as members, and parallel
`_sdl3.cpp` source files implement the same classes/functions using SDL3 APIs.
The build system selects which `.cpp` files to compile.

## Transitive Windows Dependencies (must fix first)

Even "platform-agnostic" libraries like ATCPU and ATEmulation fail to
compile on Linux because their `stdafx.h` precompiled headers and system
library includes pull in Windows-specific content transitively:

| Header | Problem | Fix |
|--------|---------|-----|
| `src/system/h/stdafx.h` | Directly includes `<windows.h>` and `<process.h>` | Guard with `#if VD_OS_WINDOWS`; on non-Windows include SDL3 equivalents |
| `src/h/vd2/system/win32/intrin.h` | MSVC-only `#pragma push_macro`/`pop_macro`, includes `<intrin.h>` | Guard pragmas with `#ifdef _MSC_VER`; on GCC/Clang use `<x86intrin.h>` (x86/x64) or `<arm_neon.h>` (ARM64) |
| `src/h/vd2/system/thread.h` lines 41-54 | Forward-declares Win32 structs (`_RTL_CRITICAL_SECTION`) and `extern "C"` Win32 functions (`InitializeCriticalSection`, etc.); `__stdcall` on `VDThread::StaticThreadStart` | Wrap in `#if VD_OS_WINDOWS`; guard `__stdcall` with `#ifdef _MSC_VER` |
| `src/h/vd2/system/atomic.h` | `#error not implemented` for compilers other than MSVC/Clang; MSVC inline asm in bitwise ops | Extend `VD_COMPILER_CLANG` checks to include `VD_COMPILER_GCC` (~10 locations) |
| `src/h/vd2/system/Error.h` line 40 | `using VDExceptionPostContext = struct HWND__ *;` | On non-Windows: `using VDExceptionPostContext = void *;` |
| `src/ATIO/h/stdafx.h` | Defines `WINVER` | Guard with `#if VD_OS_WINDOWS` |

The `win32/intrin.h` fix is straightforward since GCC and Clang both provide
`<x86intrin.h>` which is the equivalent superset of MSVC's `<intrin.h>`.
The pragmas are MSVC-specific and can be guarded with `#ifdef _MSC_VER`
(they work around a VS2005/Vista SDK conflict that doesn't exist elsewhere).

These header fixes are Phase 2 in the implementation plan -- they are small
(`#ifdef` additions only), change no behavior on Windows, and unblock
compilation of all core libraries on Linux.

## Platform Detection (vdtypes.h)

The existing `vdtypes.h` detects compiler (`VD_COMPILER_MSVC`,
`VD_COMPILER_CLANG`) and CPU architecture (`VD_CPU_AMD64`, `VD_CPU_X86`,
`VD_CPU_ARM64`) but has no OS detection and no GCC detection. Add both:

```cpp
// Compiler detection addition -- after existing VD_COMPILER_CLANG / VD_COMPILER_MSVC
#if !defined(VD_COMPILER_MSVC) && !defined(VD_COMPILER_CLANG) && defined(__GNUC__)
    #define VD_COMPILER_GCC __GNUC__
#endif

// OS detection (new)
#if defined(_WIN32)
    #define VD_OS_WINDOWS 1
#elif defined(__linux__)
    #define VD_OS_LINUX 1
#elif defined(__APPLE__)
    #define VD_OS_MACOS 1
#endif
```

GCC detection is needed because `atomic.h`, `thread.h`, and other headers
have compiler-specific paths. GCC supports the same `__sync_*` builtins as
Clang, so most code paths can be shared.

This is the only header change that has project-wide visibility.

## Threading (thread.h / thread.cpp)

### Header Changes

`VDCriticalSection` clones the `CRITICAL_SECTION` memory layout as a struct
with Win32-specific fields. On non-Windows, replace with an opaque SDL3 mutex
pointer:

```cpp
class VDCriticalSection {
private:
#if VD_OS_WINDOWS
    struct CritSec {
        void    *DebugInfo;
        sint32  LockCount;
        sint32  RecursionCount;
        void    *OwningThread;
        void    *LockSemaphore;
        uint32  SpinCount;
    } csect;
#else
    void *mMutex;  // SDL_Mutex*
#endif
    // ... public API unchanged (Lock, Unlock, AutoLock, etc.)
};
```

`VDSignalBase` holds a single `void *hEvent` member (a Win32 `HANDLE`).
`VDSemaphore` holds `void *mKernelSema`. `VDRWLock` and
`VDConditionVariable` each hold a single `void*`. These are all opaque
pointers already, but on SDL3 the signal needs *more* members (mutex +
condvar + flag for the auto-reset/manual-reset emulation):

```cpp
class VDSignalBase {
protected:
#if VD_OS_WINDOWS
    void *hEvent;  // HANDLE from CreateEvent
#else
    void *mMutex;      // SDL_Mutex*
    void *mCondition;  // SDL_Condition*
    bool mSignaled;
#endif
};
```

`VDSemaphore`, `VDRWLock`, and `VDConditionVariable` can keep their single
`void*` member since SDL3 semaphores, RW locks, and condition variables are
each a single opaque pointer.

`VDThread` holds a `VDThreadHandle` (which is `void*`) and `VDThreadID`
(which is `uint32`). SDL3 uses `SDL_Thread*` (cast to `void*`) and
`SDL_ThreadID` (which is `uint64`). Widen `VDThreadID` on non-Windows:

```cpp
#if VD_OS_WINDOWS
    typedef uint32 VDThreadID;
#else
    typedef uint64 VDThreadID;
#endif
```

`VDThread::StaticThreadStart` uses `unsigned __stdcall` calling convention
(MSVC-specific). Guard with `#ifdef _MSC_VER`; on other compilers use the
default calling convention.

The forward declarations of Win32 functions (`InitializeCriticalSection`,
etc.) at lines 49-54 must be guarded with `#if VD_OS_WINDOWS`.

### Source File: thread.cpp (cross-platform, NOT thread_sdl3.cpp)

**Note:** The original plan proposed a separate `thread_sdl3.cpp` using
SDL3 APIs.  The actual implementation uses `#ifdef VD_OS_WINDOWS` guards
inside the existing `thread.cpp`, with pthreads on non-Windows platforms.
No separate SDL3 thread file was created.

Mapping:

| VD Class | Win32 API | SDL3 API |
|----------|-----------|----------|
| `VDThread` | `_beginthreadex` | `SDL_CreateThread` |
| `VDThread::ThreadWait` | `WaitForSingleObject` | `SDL_WaitThread` |
| `VDCriticalSection` | `CRITICAL_SECTION` | `SDL_Mutex` (recursive via SDL3) |
| `VDSignal` | `CreateEvent` (auto-reset) | `SDL_Mutex` + `SDL_Condition` |
| `VDSignalPersistent` | `CreateEvent` (manual-reset) | `SDL_Mutex` + `SDL_Condition` + bool flag |
| `VDSemaphore` | `CreateSemaphore` | `SDL_Semaphore` |
| `VDRWLock` | `SRWLock` | `SDL_RWLock` |
| `VDConditionVariable` | `CONDITION_VARIABLE` | `SDL_Condition` |

Utility functions:

| Function | Win32 | SDL3 |
|----------|-------|------|
| `VDGetCurrentThreadID()` | `GetCurrentThreadId()` | `SDL_GetCurrentThreadID()` |
| `VDGetCurrentProcessId()` | `GetCurrentProcessId()` | `getpid()` (POSIX, universally available) |
| `VDGetLogicalProcessorCount()` | `GetProcessAffinityMask()` | `SDL_GetNumLogicalCPUCores()` |
| `VDSetThreadDebugName()` | `RaiseException` / `SetThreadDescription` | Thread name set at creation via `SDL_CreateThread(fn, name, data)` ; post-creation naming via `pthread_setname_np()` on Linux/macOS |
| `VDThreadSleep()` | `Sleep()` | `SDL_Delay()` |

### VDSignal Implementation Notes

`VDSignal` is an auto-reset event: `wait()` succeeds and atomically resets
the signal. SDL3 has no direct equivalent. Implement with mutex + condition
variable + boolean flag:

```cpp
void VDSignal::signal() {
    SDL_LockMutex(mMutex);
    mSignaled = true;
    SDL_SignalCondition(mCondition);
    SDL_UnlockMutex(mMutex);
}

void VDSignal::wait() {
    SDL_LockMutex(mMutex);
    while (!mSignaled)
        SDL_WaitCondition(mCondition, mMutex);
    mSignaled = false;  // auto-reset
    SDL_UnlockMutex(mMutex);
}
```

`VDSignal::wait(VDSignalBase& other)` waits on two signals simultaneously.
Win32 does this with `WaitForMultipleObjects`. SDL3 has no multi-wait. Use a
shared condition variable approach or restructure callers to use a single
signal where possible. This is the **hardest part** of the threading port and
requires case-by-case analysis of call sites.

## Wide Strings and File Paths (VDStringW / wchar_t)

This is a pervasive issue affecting all file I/O, filesystem operations,
and path handling. The codebase uses `VDStringW` (`wchar_t`-based wide
strings) for all file paths:

- `VDFile::open(const wchar_t *path, ...)` -- file open
- `VDDirectoryIterator(const wchar_t *path)` -- directory listing
- All `VDFileSplit*()`, `VDMakePath()`, etc. -- path manipulation
- `ATSimulator::Load()`, firmware paths, ROM paths, etc.

The platform difference:

| | Windows | Linux / macOS |
|---|---------|---------------|
| `wchar_t` size | 2 bytes (UTF-16) | 4 bytes (UTF-32) |
| OS file APIs | UTF-16 (`CreateFileW`) | UTF-8 (`open()`) |
| `VDStringW` | Native, matches OS | Wrong encoding for OS APIs |

### Strategy: Convert at the POSIX Boundary

The `_sdl3.cpp` implementation files convert `VDStringW` to UTF-8 at every
OS API call boundary. Conversion functions already exist in
`src/h/vd2/system/text.h`:

```cpp
VDStringA VDTextWToU8(const wchar_t *s, int length);  // wide → UTF-8
VDStringW VDTextU8ToW(const VDStringSpanA& s);         // UTF-8 → wide
```

Example usage in `file_sdl3.cpp`:

```cpp
void VDFile::open(const wchar_t *pwszFileName, uint32 flags) {
    VDStringA utf8Path = VDTextWToU8(VDStringSpanW(pwszFileName));
    int posixFlags = TranslateFlags(flags);
    mHandle = ::open(utf8Path.c_str(), posixFlags, 0666);
    // ...
}
```

And in `filesys_sdl3.cpp`:

```cpp
bool VDDirectoryIterator::Next() {
    struct dirent *entry = readdir((DIR*)mpHandle);
    if (!entry) return false;
    mFilename = VDTextU8ToW(VDStringSpanA(entry->d_name));
    // ...
}
```

This approach:
- Keeps `VDStringW` as the internal string type (no massive refactor)
- Converts only at the OS boundary (in `_sdl3.cpp` files)
- Existing Windows code is completely untouched
- The `wchar_t` size difference (2 vs 4 bytes) is irrelevant because we
  never pass `wchar_t*` directly to OS APIs on non-Windows

### Path Separator

`VDStringW` path functions already handle both `/` and `\`. The
`filesys_sdl3.cpp` implementation should normalize to `/` on non-Windows.
`VDIsPathSeparator()` already accepts both.

## File I/O (file.h / file.cpp)

### Header Changes

The only header change needed:

```cpp
#if VD_OS_WINDOWS
    typedef void *VDFileHandle;  // matches HANDLE
#else
    typedef int VDFileHandle;    // POSIX file descriptor
    // (or SDL_IOStream* if going fully SDL3, but fd is simpler
    // for the features VDFile needs like truncate, fsync, fstat)
#endif
```

Using a POSIX file descriptor rather than SDL3 IOStream here because VDFile
needs operations SDL3 IOStream does not provide: `ftruncate`, `fsync`,
`fstat` for timestamps, file locking (`fcntl`), and unbuffered/direct I/O
(`O_DIRECT`). These are standard POSIX available on both Linux and macOS.

If we strictly want to avoid POSIX and use only SDL3, we can use
`SDL_IOFromFile` for basic operations but will lose some features
(timestamps, direct I/O, file attributes). A practical approach: use POSIX
`open`/`read`/`write`/`close` on non-Windows, which is universally available
and does not introduce any external dependency.

### Source File: file_sdl3.cpp

Mapping:

| VDFile Method | Win32 | Non-Windows |
|---------------|-------|-------------|
| `open()` | `CreateFileW` | `open()` (POSIX) |
| `close()` | `CloseHandle` | `close()` |
| `read()` | `ReadFile` | `read()` |
| `write()` | `WriteFile` | `write()` |
| `seek()` | `SetFilePointer` | `lseek()` |
| `truncate()` | `SetEndOfFile` | `ftruncate()` |
| `flush()` | `FlushFileBuffers` | `fsync()` |
| `size()` | `GetFileSize` | `fstat()` |
| `getLastWriteTime()` | `GetFileTime` | `fstat()` |
| `setLastWriteTime()` | `SetFileTime` | `futimens()` |
| `extendValid()` | `SetFileValidData` (NTFS) | no-op (not needed on ext4/APFS) |
| `getAttributes()` | `GetFileAttributes` | `stat()` |

The `nsVDFile::eFlags` map to POSIX flags:

| VDFile Flag | Win32 | POSIX |
|-------------|-------|-------|
| `kRead` | `GENERIC_READ` | `O_RDONLY` |
| `kWrite` | `GENERIC_WRITE` | `O_WRONLY` |
| `kReadWrite` | both | `O_RDWR` |
| `kOpenExisting` | `OPEN_EXISTING` | 0 |
| `kCreateAlways` | `CREATE_ALWAYS` | `O_CREAT \| O_TRUNC` |
| `kCreateNew` | `CREATE_NEW` | `O_CREAT \| O_EXCL` |
| `kUnbuffered` | `FILE_FLAG_NO_BUFFERING` | `O_DIRECT` (Linux) / `fcntl F_NOCACHE` (macOS) |
| `kWriteThrough` | `FILE_FLAG_WRITE_THROUGH` | `O_SYNC` |
| `kSequential` | `FILE_FLAG_SEQUENTIAL_SCAN` | `posix_fadvise(SEQUENTIAL)` |
| `kDenyRead/Write` | share mode | `fcntl` advisory locks (best-effort) |

Note: Windows file sharing semantics (`kDenyRead`, `kDenyWrite`) have no
exact POSIX equivalent. Use advisory locking via `fcntl(F_SETLK)` where
needed, but document that enforcement is weaker on Unix.

## Async File I/O (fileasync.h / fileasync_sdl3.cpp)

**Status: Implemented.**

`IVDFileAsync` is already a pure virtual interface with a factory function.
The SDL3 implementation (`fileasync_sdl3.cpp`) uses synchronous buffered
I/O via `VDFile` rather than SDL3's async I/O API.  All `Mode` variants
behave identically (synchronous).  This is sufficient because the only
caller is `aviwriter.cpp` (AVI container writing for video recording),
which performs sequential writes with occasional seeks for header updates.

The implementation provides a 64KB write buffer for the `FastWrite()` path
and flushes on `FastWriteEnd()` or before random-access `Write()` calls.
`Extend()` is a no-op (POSIX doesn't need pre-extension).  `Truncate()`
and `SafeTruncateAndClose()` use `ftruncate()` via `VDFile::truncate()`.

The factory function `VDCreateFileAsync()` returns a `VDFileAsyncSDL3`
instance regardless of the requested mode.

## Filesystem (filesys.h / filesys.cpp)

### Path Handling

The path manipulation functions in `filesys.h` already handle both `/` and
`\` separators. For the SDL3 build, ensure:

- `VDIsPathSeparator()` accepts `/` (already does)
- `VDMakePath()` uses `/` as the default separator on non-Windows
- `VDFileGetCanonicalPath()` normalizes to `/`
- Path comparison (`VDFileIsPathEqual`) is case-sensitive on non-Windows

### Filesystem Operations

| Function | Win32 | SDL3 / POSIX |
|----------|-------|-------------|
| `VDCreateDirectory()` | `CreateDirectoryW` | `SDL_CreateDirectory()` |
| `VDRemoveDirectory()` | `RemoveDirectoryW` | `SDL_RemovePath()` |
| `VDRemoveFile()` | `DeleteFileW` | `SDL_RemovePath()` |
| `VDMoveFile()` | `MoveFileW` | `SDL_RenamePath()` |
| `VDDoesPathExist()` | `GetFileAttributesW` | `SDL_GetPathInfo()` |
| `VDFileGetAttributes()` | `GetFileAttributesW` | `stat()` |
| `VDFileGetLastWriteTime()` | `GetFileTime` | `stat()` |
| `VDGetFullPath()` | `GetFullPathNameW` | `realpath()` |
| `VDGetLocalModulePath()` | `GetModuleFileNameW` | `SDL_GetBasePath()` |
| `VDGetProgramPath()` | `GetModuleFileNameW` | `SDL_GetBasePath()` |
| `VDGetDiskFreeSpace()` | `GetDiskFreeSpaceExW` | `statvfs()` |
| `VDDirectoryIterator` | `FindFirstFileW` / `FindNextFileW` | `SDL_EnumerateDirectory()` or `opendir`/`readdir` |

### Source File: filesys_sdl3.cpp

Most operations map cleanly to either SDL3 or standard POSIX calls. The
`VDDirectoryIterator` class stores its search handle as `void *mpHandle`
(Win32 `HANDLE` from `FindFirstFileW`). On non-Windows, this becomes a
`DIR*` (cast to `void*`) and iteration uses `readdir()`, or SDL3's
`SDL_EnumerateDirectory()` with a callback collecting results. The public
API and member types are unchanged since `mpHandle` is already `void*`.

## Configuration Storage (registry.h / registry.cpp)

`IVDRegistryProvider` is already a pure virtual interface with a global
setter (`VDSetRegistryProvider`). The Windows build uses
`VDGetDefaultRegistryProvider()` which accesses `HKEY_CURRENT_USER`.

### Implementation (Actual)

The SDL3 build reuses `VDRegistryProviderMemory` (the existing in-memory
provider) and persists settings to an INI-like text file using the **same
format as Windows Altirra's portable mode**. This avoids writing a new
JSON provider and leverages the existing, battle-tested `ATUILoadRegistry`
/ `ATUISaveRegistry` functions from `uiregistry.cpp`.

**File location:** `~/.config/altirra/settings.ini` (XDG Base Directory)

**Format:** INI-like sections matching Windows portable mode:
```ini
; Altirra settings file. EDIT AT YOUR OWN RISK.

[User\AltirraSDL]
"Hardware mode" = 1
"Video standard" = 0
"BASIC enabled" = 1

[User\AltirraSDL\Profiles\00000000]
"Name" = "Default"
```

**Key files:**
- `src/system/source/registry_sdl3.cpp` — Provider + load/save wrappers
- `src/Altirra/source/uiregistry.cpp` — INI serialization (shared with Windows)

**Lifecycle:**
1. `VDRegistryAppKey::setDefaultKey("AltirraSDL")` — set app key prefix
2. `ATRegistryLoadFromDisk()` — load INI into `VDRegistryProviderMemory`
3. `ATLoadSettings(...)` — read from memory provider into simulator
4. *(emulator runs)*
5. `ATSaveSettings(...)` — write simulator state into memory provider
6. `ATRegistryFlushToDisk()` — serialize memory provider to INI file

The `VDRegistryAppKey` and `VDRegistryKey` convenience classes work
unchanged since they use the provider interface. Settings files are
cross-compatible with Windows Altirra's portable mode format.

## Atomics (atomic.h)

The header has two main code paths for core operations (exchange, add,
compare-exchange):

1. `VD_COMPILER_MSVC` -- uses `_Interlocked*` intrinsics
2. `VD_COMPILER_CLANG` -- uses `__sync_*` builtins
3. `#else` -- **`#error not implemented`**

For bitwise operations (`&=`, `|=`, `^=`), there is a third path using MSVC
inline assembly (`__asm lock and ...`), but this syntax is MSVC-specific and
won't compile with GCC.

**GCC on Linux will hit `#error`.** Fix by extending the Clang check to
include GCC:

```cpp
// Change from:
#elif defined(VD_COMPILER_CLANG)
// To:
#elif defined(VD_COMPILER_CLANG) || defined(VD_COMPILER_GCC)
```

GCC supports the same `__sync_*` builtins as Clang. This is a safe change
(1-line edit per operation, ~10 locations) and does not affect the MSVC path.

Alternatively, modernize to `<atomic>` from C++17, but that is a larger
refactor with no immediate benefit beyond this porting need.

## Timers (timerserviceimpl_win32.h)

`ATCore` has a timer service implementation using Win32 thread pool timers.
The interface (`IATTimerService`) is clean. Create a parallel implementation:

```cpp
// timerserviceimpl_sdl3.h
// Uses SDL_AddTimerNS() for high-resolution callback scheduling
```

SDL3 timers fire callbacks on a separate thread, matching the Win32 thread
pool timer behavior.

## Summary of Files

### Modified (minimal #ifdef additions)

| File | Change |
|------|--------|
| `src/h/vd2/system/vdtypes.h` | Add `VD_COMPILER_GCC` detection and `VD_OS_*` macros (~10 lines) |
| `src/h/vd2/system/win32/intrin.h` | Guard MSVC pragmas; add `<x86intrin.h>` path for GCC/Clang (~10 lines) |
| `src/h/vd2/system/thread.h` | `#ifdef` for `VDCriticalSection` members, `VDSignalBase` members, guard Win32 forward declarations, guard `__stdcall`, widen `VDThreadID` (~50 lines total) |
| `src/h/vd2/system/atomic.h` | Extend `VD_COMPILER_CLANG` checks to include `VD_COMPILER_GCC` (~10 one-line edits) |
| `src/h/vd2/system/Error.h` | Guard `HWND__` typedef; use `void*` on non-Windows (~3 lines) |
| `src/h/vd2/system/file.h` | Replace `#error` with `typedef int VDFileHandle` (~2 lines) |
| `src/system/h/stdafx.h` | Guard `<windows.h>` and `<process.h>` includes (~10 lines) |
| `src/ATIO/h/stdafx.h` | Guard `WINVER` define (~3 lines) |

### New Files

| File | Purpose |
|------|---------|
| `src/system/source/thread_sdl3.cpp` | VDThread, VDCriticalSection, VDSignal, VDSemaphore, VDRWLock, VDConditionVariable |
| `src/system/source/file_sdl3.cpp` | VDFile (POSIX open/read/write/close) |
| `src/system/source/filesys_sdl3.cpp` | Path operations, directory iteration, filesystem queries |
| `src/system/source/fileasync_sdl3.cpp` | IVDFileAsync using synchronous buffered VDFile I/O |
| `src/system/source/registry_sdl3.cpp` | IVDRegistryProvider backed by JSON file |
| `src/ATCore/source/timerserviceimpl_sdl3.h` | IATTimerService using SDL3 timers |

### Unchanged (compile as-is on all platforms)

All files in: `ATCPU`, `ATEmulation`, `ATDevices`, `ATIO`, `ATNetwork`,
`ATCompiler`, `ATVM`, `ATDebugger`, `ATBasic`, `vdjson`, `Kasumi` (verified:
zero Win32 dependencies in all 37 source files), and the synthesis portion
of `ATAudio` (pokey, filters, sample players).

**Not compiled for SDL3 build:** `VDDisplay` (15+ files with GDI/D3D deps),
`Dita` (COM/shell deps in `services.cpp`), `Riza` (Win32 audio backends),
`Tessa`, `ATNativeUI`, `ATUI`, `ATUIControls`. See [BUILD.md](BUILD.md) for
the full exclusion list.
