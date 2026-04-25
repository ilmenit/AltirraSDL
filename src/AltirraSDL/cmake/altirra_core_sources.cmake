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
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/profilerui\\.cpp$")        # Win32 profiler UI (w32assist.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/startuplogger\\.cpp$")     # Win32 only (windows.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/texteditor\\.cpp$")        # Win32 HWND/WndProc text editor widget

# midimate_sdl3.cpp handles all platforms internally (winmm on Windows,
# ALSA on Linux, CoreMIDI on macOS, no-op on Android/WASM), so the
# original midimate.cpp stays excluded everywhere — its factory symbol
# would clash if both linked.
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/midimate\\.cpp$")

# idephysdisk.cpp / modemtcp.cpp are Win32-only (DeviceIoControl,
# Winsock2). On Windows-SDL3 we use the originals; on POSIX targets
# the source/os/*_sdl3.cpp replacements are linked instead.
if(NOT WIN32)
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/idephysdisk\\.cpp$")
    list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/modemtcp\\.cpp$")
endif()

# pipeserial_win32.cpp is the Win32 named-pipe implementation. It was
# already excluded by the blanket _win32.cpp filter above; on Windows
# we re-add it explicitly so Windows-SDL3 builds get the native impl.
if(WIN32)
    list(APPEND ALTIRRA_ALL_SOURCES
        "${CMAKE_SOURCE_DIR}/src/Altirra/source/pipeserial_win32.cpp")
endif()

# customdevice_win32.cpp is misnamed: despite the suffix it has no Win32
# deps (only at/atnetworksockets/nativesockets.h, which is portable).
# The blanket _win32.cpp filter above would strip it, so re-add it
# explicitly. customdevice.cpp + customdevicevmtypes.cpp are also
# portable (atui/uicommandmanager.h is provided by AltirraSDL/stubs/).
list(APPEND ALTIRRA_ALL_SOURCES "${CMAKE_SOURCE_DIR}/src/Altirra/source/customdevice_win32.cpp")

# uiqueue.cpp is portable (only depends on uicommondialogs.h's
# ATUIShowAlertError, stubbed in AltirraSDL/stubs/uiaccessors_stubs.cpp).
# The Custom Device VM uses ATUIGetQueue + ATUIPushStep to defer command
# execution to the next UI tick — see customdevicevmtypes.cpp:1728.
# The blanket ui*.cpp filter above strips it, so re-add explicitly.
list(APPEND ALTIRRA_ALL_SOURCES "${CMAKE_SOURCE_DIR}/src/Altirra/source/uiqueue.cpp")

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
