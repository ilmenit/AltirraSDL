# AltirraBridgeServer — headless lean SDK target.
#
# Speaks the same bridge protocol as `AltirraSDL --bridge`, but with no
# Dear ImGui, no librashader, no display backend, no input layer, no UI
# tree, no embedded fonts, and no SDL3. The audio mixer is compiled
# from audiooutput_sdl3.cpp with -DALTIRRA_AUDIO_NULL, which replaces
# the SDL_AudioStream output with a silent null backend (samples are
# generated and discarded). Screenshot / rawscreen / render_frame are
# unaffected: they pull XRGB8888 frames from GTIA's frame buffer via
# ATBridgeNullVideoDisplay (ui_stubs.cpp) and encode PNG with the
# vendored encoder in bridge_commands_render.cpp — none of which
# touches SDL3.
#
# This target is opt-in via -DALTIRRA_BRIDGE_SERVER=ON, and depends on
# the AltirraSDL target for two things:
#   1. ALTIRRA_ALL_SOURCES (the filtered emulation core source list,
#      computed in src/AltirraSDL/cmake/altirra_core_sources.cmake)
#   2. The Win32 stub files (stubs/console_stubs.cpp, etc.) and several
#      glue files that live under src/AltirraSDL/source/ but are
#      themselves portable.

set(ALTIRRA_SDL_DIR ${CMAKE_SOURCE_DIR}/src/AltirraSDL)

add_executable(AltirraBridgeServer
    main_bridge.cpp
    # No-op stubs for SDL3-frontend symbols that the AltirraSDL stubs/
    # files reference but the bridge server has no real implementation
    # for. Includes ATBridgeRequestAppQuit (which the bridge module
    # calls on QUIT) and ATUIPushDeferred (matched-mangled-name
    # no-op so uiaccessors_stubs.cpp's ATUIBootImage links).
    ui_stubs.cpp

    # --- All emulation core files (filtered, shared with AltirraSDL) ---
    ${ALTIRRA_ALL_SOURCES}

    # --- Stub implementations replacing Win32-only files ---
    # Same stubs the SDL3 frontend uses; they're plain C++ no-ops or
    # POSIX equivalents and depend on nothing UI-related.
    ${ALTIRRA_SDL_DIR}/stubs/console_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/oshelper_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/uiaccessors_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/win32_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/device_stubs.cpp

    # Portable polling-based directory watcher (replaces the Win32-only
    # directorywatcher.cpp). diskvirtimage.cpp / diskvirtimagesdfs.cpp
    # link against ATDirectoryWatcher even in the bridge server build.
    ${ALTIRRA_SDL_DIR}/source/os/directorywatcher_sdl3.cpp

    # Portable POSIX TCP modem driver (replaces Winsock modemtcp.cpp).
    # The SX-212/850 modem devices link against ATCreateModemDriverTCP
    # even in headless mode.
    ${ALTIRRA_SDL_DIR}/source/os/modemtcp_sdl3.cpp

    # Portable POSIX raw block-device passthrough (replaces Win32
    # idephysdisk.cpp). IDE adapter devices link against ATIDEPhysicalDisk
    # via idediskdevices.cpp even in headless mode.
    ${ALTIRRA_SDL_DIR}/source/os/idephysdisk_sdl3.cpp

    # MidiMate device with portable MIDI backend. Headless builds get
    # the ALSA backend on Linux (audible if a target is connected via
    # aconnect) — same source as the GUI build.
    ${ALTIRRA_SDL_DIR}/source/os/midimate_sdl3.cpp

    # PipeSerial device on UNIX-domain sockets (replaces Win32-only
    # pipeserial_win32.cpp). Bridge clients can connect to e.g.
    # $XDG_RUNTIME_DIR/altirra-AltirraSerial.sock to drive the SIO bus.
    ${ALTIRRA_SDL_DIR}/source/os/pipeserial_sdl3.cpp

    # Keymap port — provides ATUIInitVirtualKeyMap, ATUIGet*KeyMap,
    # ATUIGetScanCodeForVirtualKey/Character32, etc.  settings.cpp links
    # against these symbols.  Self-contained, depends only on
    # vd2/system + uikeyboard.h.
    ${ALTIRRA_SDL_DIR}/source/input/keyboard_keymap_sdl3.cpp

    # --- AltirraSDL platform glue that's UI-free ---
    # logging.h's LOG_INFO/LOG_ERROR are header-only macros, no .cpp.
    # crash_report.cpp is intentionally NOT included — it uses
    # ImGui::Begin/Button/etc. for the in-app crash viewer, and the
    # bridge server has no UI. ATCrashReport* symbols are not
    # referenced from the core emulation path.
    # Settings persistence — same INI loader as AltirraSDL.
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp
    # Save state deserializer registration.
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/savestateio.cpp
    # NB: source/ui/core/ui_indicators.cpp is intentionally NOT
    # included — it actively uses ImGui (ImDrawList, GetForegroundDrawList,
    # IM_COL32) and pulling it in would force a real ImGui dependency.
    # Instead, ui_stubs.cpp provides a minimal IATUIRenderer
    # implementation with all methods as no-ops, plus the
    # ATCreateUIRenderer factory.

    # ATUICommandManager (from ATUI library) — referenced by debuggerautotest.cpp
    ${CMAKE_SOURCE_DIR}/src/ATUI/source/uicommandmanager.cpp

    # VDAccelTableDefinition — accelerator tables, used by command registration
    ${CMAKE_SOURCE_DIR}/src/Dita/source/accel.cpp

    # VDDisplay utility sources (still needed by some core code paths
    # even though we don't render anything)
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/screenfx.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/bloom.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/bicubic.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/displaytypes.cpp
    # NB: VDDisplay/source/display.cpp pulls in <windows.h> via
    # w32assist.h and isn't portable. The few VDVideoDisplayFrame
    # methods that ATFrameBuffer (in gtia.cpp) derives from are
    # provided as no-op stubs in ui_stubs.cpp instead — same trick
    # display_sdl3.cpp uses for the SDL3 frontend.

    # Audio output — same mixer source as AltirraSDL, but compiled with
    # -DALTIRRA_AUDIO_NULL (see target_compile_definitions below) so all
    # SDL calls are stubbed and no device is opened.
    ${CMAKE_SOURCE_DIR}/src/ATAudio/source/audiooutput_sdl3.cpp

    # --- Bridge module (shared with AltirraSDL via path reference) ---
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_server.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_protocol.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_transport.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_commands_state.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_commands_write.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_commands_render.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_commands_debug.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_commands_debug2.cpp
    ${ALTIRRA_SDL_DIR}/source/bridge/bridge_bare_stub.cpp
    # NB: bridge_main_glue_sdl3.cpp is NOT included — main_bridge.cpp
    # provides its own ATBridgeDispatch* implementations that call the
    # simulator directly without the SDL3 deferred-action queue.
)

# Re-include Win32-specific support that the SDL3 build also re-includes.
if(WIN32)
    target_sources(AltirraBridgeServer PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Altirra/source/pclink_win32.cpp
    )
endif()

target_compile_definitions(AltirraBridgeServer PRIVATE
    ALTIRRA_BRIDGE_ENABLED=1
    ALTIRRA_BRIDGE_HEADLESS=1
    ALTIRRA_AUDIO_NULL=1
    # AT_SDL3_PORTABLE=1 enables the cross-platform code paths in
    # the Altirra core that the SDL3 frontend turns on. Without this,
    # several headers fall back to Win32-only paths and we get
    # compile errors. Same value as AltirraSDL's build flags.
    AT_SDL3_PORTABLE=1
)

# Same include paths as AltirraSDL for the headers it shares. We do
# NOT include the SDL3 frontend's source/app, source/display, etc.
# subdirectories — main_bridge.cpp must not reach into those.
target_include_directories(AltirraBridgeServer PRIVATE
    # Public Altirra headers (src/h/at/*) — REQUIRED for breakpoints.h,
    # audiomixer.h, and other core interfaces. Must come before
    # Altirra/h to match AltirraSDL's include order.
    ${ALTIRRA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Altirra/h
    ${CMAKE_SOURCE_DIR}/src/Altirra/source
    # NB: src/compat is NOT added here unconditionally. It contains
    # non-MSVC shims for <tchar.h> / <intrin.h>; on MSVC the shim
    # tchar.h shadows the real Windows tchar.h and produces
    # "TCHAR: redefinition; different basic types" errors. It is
    # added conditionally below via ALTIRRA_COMPAT_DIR (only set
    # on non-MSVC in the top-level CMakeLists.txt), mirroring how
    # src/AltirraSDL/CMakeLists.txt handles it.
    ${ALTIRRA_SDL_DIR}/source
    ${ALTIRRA_SDL_DIR}/source/app          # logging.h, crash_report.h
    ${ALTIRRA_SDL_DIR}/source/bridge       # bridge_*.h
    # uiaccessors_stubs.cpp #includes "ui_main.h" — that header is
    # mostly types/declarations, no SDL3 deps. We provide just the
    # one include path needed for the stub to compile, NOT the
    # entire SDL3 frontend source tree.
    ${ALTIRRA_SDL_DIR}/source/ui/core
    ${CMAKE_SOURCE_DIR}/src/ATAudio/h
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/h
    ${CMAKE_SOURCE_DIR}/src/Altirra/res
    ${CMAKE_SOURCE_DIR}/src/Altirra/autogen
    ${CMAKE_SOURCE_DIR}/src/Altirra/autobuild_default
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(AltirraBridgeServer PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

# Stubs come BEFORE Altirra headers (same as AltirraSDL) so the stub
# versions of Win32-dependent headers are found first.
#
# We also prepend our own stubs/ directory which provides a minimal
# imgui.h shim — uiaccessors_stubs.cpp transitively #includes
# <imgui.h> via ui_main.h, but the bridge server doesn't link Dear
# ImGui. The shim defines just enough types (ImVec2, ImGuiKey, etc.)
# for the include chain to compile; calling any ImGui:: function
# from this build is a link-time error by design.
target_include_directories(AltirraBridgeServer BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${ALTIRRA_SDL_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/src/Altirra/h
)

# Drop unused NEEDED entries (specifically libSDL3, which the `system`
# library re-exports as PUBLIC for the AltirraSDL frontend but which
# the headless bridge server does not actually call into — all SDL
# symbols are replaced by std::chrono / no-op stubs). With `--as-needed`
# the dynamic linker only records libraries that resolve a symbol the
# binary really uses.
if(UNIX AND NOT APPLE)
    target_link_options(AltirraBridgeServer PRIVATE "LINKER:--as-needed")
elseif(APPLE)
    # ld64 equivalent of --as-needed: drop LC_LOAD_DYLIB entries for
    # dylibs with no resolved symbols in the final image. Without this
    # the binary would carry an unused libSDL3.dylib load command
    # inherited from the `system` library's PUBLIC link.
    target_link_options(AltirraBridgeServer PRIVATE "LINKER:-dead_strip_dylibs")
endif()
# Windows: no action needed. On Windows the `system` library does not
# link SDL3 at all (src/system/CMakeLists.txt takes the winmm/shlwapi
# branch), so no transitive SDL3 dependency exists to strip.

target_link_libraries(AltirraBridgeServer PRIVATE
    # Core emulation libraries (same set as AltirraSDL)
    ATCPU
    ATEmulation
    ATDevices
    ATIO
    ATAudio
    ATNetwork
    ATNetworkSockets
    ATDebugger
    ATVM
    ATCore
    Kasumi
    vdjson
    system
)

# Inherit CMAKE_CXX_STANDARD from the top-level project (currently 23).
# Do NOT downgrade — several Altirra headers use C++23 lambda template
# features that won't compile under C++17.

# Compile definitions to match AltirraSDL's platform classification
# (the core emulation files conditionally compile against these).
if(WIN32)
    target_compile_definitions(AltirraBridgeServer PRIVATE VD_OS_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(AltirraBridgeServer PRIVATE VD_OS_MACOS=1)
    # CoreMIDI for the MidiMate device backend.
    target_link_libraries(AltirraBridgeServer PRIVATE
        "-framework CoreFoundation"
        "-framework CoreMIDI"
    )
elseif(UNIX)
    target_compile_definitions(AltirraBridgeServer PRIVATE VD_OS_LINUX=1)
    # ALSA seq for the MidiMate device backend (matches AltirraSDL).
    if(NOT ANDROID)
        find_package(ALSA)
        if(ALSA_FOUND)
            target_link_libraries(AltirraBridgeServer PRIVATE ALSA::ALSA)
        else()
            target_compile_definitions(AltirraBridgeServer PRIVATE
                ATMIDI_BACKEND_NULL=1)
        endif()
    endif()
endif()

# MSVC + Windows SDK 10.0.26100 UCRT <wchar.h> contains inline
# functions that unconditionally use __m128i/__m256i and the _mm_*
# intrinsic family. If <wchar.h> is pulled in (via <tchar.h>,
# <math.h>, or <string.h>) before the intrinsics are declared, the
# compiler chokes on undeclared identifiers. Force-include <intrin.h>
# so every TU in this target starts with the full intrinsic set
# declared. The main AltirraSDL target doesn't need this because its
# .sln / VS-generator path pulls the intrinsics via a different
# include chain; the Ninja+cl bridge build does.
# Per-file SIMD flags. Mirror src/AltirraSDL/CMakeLists.txt — these
# files use intrinsics that need -mbmi / -mssse3 to compile under
# GCC/Clang on x86_64.
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/Altirra/source/antic.cpp"
        PROPERTIES COMPILE_FLAGS "-mbmi"
    )
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/Altirra/source/tracefileencoding.cpp"
        PROPERTIES COMPILE_FLAGS "-mssse3 -mpopcnt"
    )
endif()
