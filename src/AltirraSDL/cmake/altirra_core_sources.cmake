# Computes ALTIRRA_ALL_SOURCES — the set of platform-agnostic Altirra
# emulation source files, with Win32-only and UI-only files filtered
# out. Both the AltirraSDL frontend and the AltirraBridgeServer
# headless target consume this same list, so the filter logic lives
# here to avoid duplication.
#
# This file is included from CMakeLists.txt via include(...). It
# leaves ALTIRRA_ALL_SOURCES set in the caller's scope.

file(GLOB ALTIRRA_ALL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/Altirra/source/*.cpp"
)

# Filter out Win32 UI, entry point, and platform-specific files
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/ui[a-zA-Z][^/]*\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_win32\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/oshelper\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/console\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/videowriter\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/aviwriter\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/directorywatcher\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/joystick\\.cpp$")
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/safewrite\\.cpp$")
# Exclude files that use Win32-only APIs and have no emulation purpose
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/about\\.cpp$")       # richedit.h
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/allocator\\.cpp$")   # crtdbg.h (MSVC debug heap)
# Exclude UI command handler files (include atnativeui/uiframe.h -> windows.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/cmd[a-z][^/]*\\.cpp$")
# Exclude files with Win32 UI dependencies not needed for emulation core
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/customdevice\\.cpp$")      # includes customdevice_win32.h
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/customdevicevmtypes\\.cpp$") # includes atui/uicommandmanager.h
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/cs8900a\\.cpp$")           # Win32 UI debug path
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/idephysdisk\\.cpp$")       # Win32 raw disk access (DeviceIoControl)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/midimate\\.cpp$")          # Win32 MIDI API (winmm.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/modemtcp\\.cpp$")          # Win32 networking (windows.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/profilerui\\.cpp$")        # Win32 profiler UI (w32assist.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/startuplogger\\.cpp$")     # Win32 only (windows.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/texteditor\\.cpp$")        # Win32 HWND/WndProc text editor widget

# Exclude architecture-specific files for the wrong target
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_neon\\.cpp$")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_arm64\\.cpp$")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[36]86)")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_sse2\\.cpp$")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_sse2_intrin\\.cpp$")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_ssse3\\.cpp$")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*_avx2\\.cpp$")
endif()
