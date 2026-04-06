cmake_minimum_required(VERSION 3.24)

# Collect all Altirra simulator source files
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
# debugger.cpp now compiles via stub headers (stubs/at/atnativeui/uiframe.h)
# debuggerautotest.cpp now compiles via stub headers (stubs/at/atui/uicommandmanager.h)
# cpuheatmap.cpp has no Win32 deps (only uses console.h, cpu.h, simeventmanager.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/customdevice\\.cpp$")      # includes customdevice_win32.h
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/customdevicevmtypes\\.cpp$") # includes atui/uicommandmanager.h
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/cs8900a\\.cpp$")           # Win32 UI debug path
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/idephysdisk\\.cpp$")       # Win32 raw disk access (DeviceIoControl)
# idevhdimage.cpp is now portable (UTF-16LE encoding handled for non-Windows)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/midimate\\.cpp$")          # Win32 MIDI API (winmm.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/modemtcp\\.cpp$")          # Win32 networking (windows.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/profilerui\\.cpp$")        # Win32 profiler UI (w32assist.h)
# settings.cpp is now compiled for SDL3 — Win32 UI deps are satisfied by
# stubs/uiaccessors_stubs.cpp and stub headers in stubs/.
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/startuplogger\\.cpp$")     # Win32 only (windows.h)
list(FILTER ALTIRRA_ALL_SOURCES EXCLUDE REGEX ".*/texteditor\\.cpp$")        # Win32 HWND/WndProc text editor widget
# vgmwriter.cpp is now portable (UTF-16LE encoding fixed for non-Windows)
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

# On Android, the native code is a shared library loaded by SDLActivity.
# On desktop, it's a regular executable.
if(ANDROID)
    add_library(AltirraSDL SHARED)
    # Android APK expects the library to be named "libmain.so"
    set_target_properties(AltirraSDL PROPERTIES OUTPUT_NAME "main")
else()
    add_executable(AltirraSDL)
endif()

target_sources(AltirraSDL PRIVATE
    # --- app/ : process lifecycle, main loop, CLI, crash ---
    source/app/main_sdl3.cpp
    source/app/main_android_bridge.cpp
    source/app/commandline_sdl3.cpp
    source/app/crash_report.cpp
    source/app/android_platform.cpp

    # --- display/ : SDL display + GL backends + shaders ---
    source/display/display_sdl3.cpp
    source/display/display_backend_sdl.cpp
    source/display/display_backend_gl33.cpp
    source/display/gl_funcs.cpp
    source/display/gl_helpers.cpp
    source/display/display_librashader.cpp

    # --- input/ : SDL input, joystick, touch ---
    source/input/input_sdl3.cpp
    source/input/joystick_sdl3.cpp
    source/input/touch_layout.cpp
    source/input/touch_controls.cpp
    source/input/touch_widgets.cpp

    # --- media/ : video recording, accelerator table ---
    source/media/videowriter_sdl3.cpp
    source/media/accel_sdl3.cpp
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/aviwriter.cpp

    # --- ui/core/ : ImGui hub, main render, indicators ---
    source/ui/core/ui_main.cpp
    source/ui/core/ui_main_aboutdialogs.cpp
    source/ui/core/ui_indicators.cpp

    # --- ui/menus/ : menu bar ---
    source/ui/menus/ui_menus.cpp
    source/ui/menus/ui_menus_view.cpp
    source/ui/menus/ui_menus_system.cpp
    source/ui/menus/ui_menus_debug.cpp

    # --- ui/sysconfig/ : Configure System paged dialog ---
    source/ui/sysconfig/ui_system.cpp
    source/ui/sysconfig/ui_system_pages_computer.cpp
    source/ui/sysconfig/ui_system_pages_outputs.cpp
    source/ui/sysconfig/ui_system_pages_b.cpp

    # --- ui/firmware/ : Firmware Manager + category ---
    source/ui/firmware/ui_firmware.cpp
    source/ui/firmware/ui_firmware_category.cpp

    # --- ui/devconfig/ : Device configuration dialogs ---
    source/ui/devconfig/ui_devconfig.cpp
    source/ui/devconfig/ui_devconfig_devices.cpp
    source/ui/devconfig/ui_devconfig_devices_b.cpp
    source/ui/devconfig/ui_devconfig_devices_browse.cpp

    # --- ui/input_config/ : Input mapping UI ---
    source/ui/input_config/ui_input.cpp
    source/ui/input_config/ui_input_setup.cpp

    # --- ui/tools/ : Advanced Config, Disk Explorer, Setup Wizard, etc. ---
    source/ui/tools/ui_tools.cpp
    source/ui/tools/ui_tools_diskexplorer.cpp
    source/ui/tools/ui_tools_setup_wizard.cpp
    source/ui/tools/ui_tools_shortcuts.cpp
    source/ui/tools/ui_tools_compatdb.cpp

    # --- ui/dialogs/ : standalone single-purpose dialogs ---
    source/ui/dialogs/ui_disk.cpp
    source/ui/dialogs/ui_cassette.cpp
    source/ui/dialogs/ui_cartmapper.cpp
    source/ui/dialogs/ui_cheater.cpp
    source/ui/dialogs/ui_calibrate.cpp
    source/ui/dialogs/ui_profiles.cpp
    source/ui/dialogs/ui_recording.cpp
    source/ui/dialogs/ui_rewind.cpp
    source/ui/dialogs/ui_emuerror.cpp
    source/ui/dialogs/ui_progress.cpp
    source/ui/dialogs/ui_lightpen.cpp
    source/ui/dialogs/ui_palette_solver.cpp
    source/ui/dialogs/ui_screenfx.cpp
    source/ui/dialogs/ui_shader_presets.cpp
    source/ui/dialogs/ui_display.cpp
    source/ui/dialogs/ui_hud_customize.cpp
    source/ui/dialogs/ui_keyboard_customize.cpp
    source/ui/dialogs/ui_keyboard_customize_tables.cpp
    source/ui/dialogs/ui_textselection.cpp

    # --- ui/debugger/ : Debugger pane manager and panes ---
    source/ui/debugger/ui_debugger.cpp
    source/ui/debugger/ui_dbg_console.cpp
    source/ui/debugger/ui_dbg_registers.cpp
    source/ui/debugger/ui_dbg_disassembly.cpp
    source/ui/debugger/ui_dbg_history.cpp
    source/ui/debugger/ui_dbg_history_format.cpp
    source/ui/debugger/ui_dbg_memory.cpp
    source/ui/debugger/ui_dbg_memory_hexdump.cpp
    source/ui/debugger/ui_dbg_memory_bitmap.cpp
    source/ui/debugger/ui_dbg_breakpoints.cpp
    source/ui/debugger/ui_dbg_callstack.cpp
    source/ui/debugger/ui_dbg_watch.cpp
    source/ui/debugger/ui_dbg_targets.cpp
    source/ui/debugger/ui_dbg_printer.cpp
    source/ui/debugger/ui_dbg_debugdisplay.cpp
    source/ui/debugger/ui_dbg_source.cpp
    source/ui/debugger/ui_dbg_verifier.cpp
    source/ui/debugger/ui_dbg_traceviewer.cpp
    source/ui/debugger/ui_dbg_traceviewer_timeline.cpp
    source/ui/debugger/ui_dbg_traceviewer_panels.cpp
    source/ui/debugger/ui_dbg_traceviewer_history_panel.cpp
    source/ui/debugger/ui_dbg_traceviewer_profile_panel.cpp
    source/ui/debugger/ui_dbg_traceviewer_log_panel.cpp

    # --- ui/tapeeditor/ : Tape Editor (waveform, editing, analysis) ---
    source/ui/tapeeditor/ui_tool_tapeeditor.cpp
    source/ui/tapeeditor/ui_tool_tapeeditor_view.cpp
    source/ui/tapeeditor/ui_tool_tapeeditor_ops.cpp

    # --- ui/testmode/ : Automated testing via IPC (Unix sockets / named pipes) ---
    source/ui/testmode/testmode_ipc.cpp
    source/ui/testmode/ui_testmode.cpp

    # --- ui/mobile/ : Mobile touch UI (Android) ---
    source/ui/mobile/ui_mobile.cpp
    source/ui/mobile/mobile_hamburger.cpp
    source/ui/mobile/mobile_file_browser.cpp
    source/ui/mobile/mobile_settings.cpp
    source/ui/mobile/mobile_disk.cpp
    source/ui/mobile/mobile_about_wizard.cpp

    # Registry persistence (filtered out by ui* regex, so add explicitly)
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp

    # ATUICommandManager (from ATUI library, no Win32 deps — needed by debuggerautotest.cpp)
    ${CMAKE_SOURCE_DIR}/src/ATUI/source/uicommandmanager.cpp

    # VDAccelTableDefinition (from Dita library, no Win32 deps — accelerator tables)
    ${CMAKE_SOURCE_DIR}/src/Dita/source/accel.cpp

    # SDL3 command handlers (accel_sdl3.cpp already listed in media/ group)
    source/commands_sdl3.cpp

    # Stub implementations replacing Win32-only files
    stubs/console_stubs.cpp
    stubs/oshelper_stubs.cpp
    # joystick_stubs.cpp replaced by source/input/joystick_sdl3.cpp
    stubs/uiaccessors_stubs.cpp
    stubs/win32_stubs.cpp
    stubs/device_stubs.cpp

    # VDDisplay utility sources (screen FX texture generation, bloom, bicubic)
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/screenfx.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/bloom.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/bicubic.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/displaytypes.cpp

    # Audio output
    ${CMAKE_SOURCE_DIR}/src/ATAudio/source/audiooutput_sdl3.cpp

    # All emulation core files (filtered above)
    ${ALTIRRA_ALL_SOURCES}
)

# Re-include Win32-specific files that were excluded by the _win32.cpp filter
# but are needed on the Windows SDL3 build.
if(WIN32)
    target_sources(AltirraSDL PRIVATE
        # ATTranslateWin32ErrorToSIOError — used by hostdevice.cpp and pclink.cpp
        # under #ifdef _WIN32 guards.
        ${CMAKE_SOURCE_DIR}/src/Altirra/source/pclink_win32.cpp
    )
endif()


target_include_directories(AltirraSDL PRIVATE
    # Public system headers
    ${ALTIRRA_INCLUDE_DIR}
    # Altirra private headers (simulator.h, gtia.h, etc.)
    ${CMAKE_SOURCE_DIR}/src/Altirra/h
    # Altirra source dir (for #include "foo.h" without path)
    ${CMAKE_SOURCE_DIR}/src/Altirra/source
    # SDL3 frontend headers (flat include path + per-feature subfolders so
    # existing `#include "ui_main.h"` style references keep resolving after
    # the Phase 1 reorganisation)
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/app
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/display
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/input
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/media
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/core
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/menus
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/sysconfig
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/firmware
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/devconfig
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/input_config
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/tools
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/dialogs
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/debugger
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/tapeeditor
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/testmode
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui/mobile
    # ATAudio headers
    ${CMAKE_SOURCE_DIR}/src/ATAudio/h
    # VDDisplay internal headers (screenfx.h, bloom.h, bicubic.h)
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/h
    # Resource ID constants (#define IDD_*, IDR_*, etc.)
    ${CMAKE_SOURCE_DIR}/src/Altirra/res
    # Auto-generated binary blobs (pbidisk.inl, etc.)
    ${CMAKE_SOURCE_DIR}/src/Altirra/autogen
    # Default version.h for local/cross-platform builds
    ${CMAKE_SOURCE_DIR}/src/Altirra/autobuild_default
)

# Compat shims (intrin.h, tchar.h) for non-MSVC compilers
if(ALTIRRA_COMPAT_DIR)
    target_include_directories(AltirraSDL PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

# Stubs directory MUST come before Altirra headers so that stub versions
# of Win32-dependent headers (uiportmenus.h, at/atui/uimanager.h, etc.)
# are found first.  The BEFORE keyword prepends to the include list.
target_include_directories(AltirraSDL BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/stubs
    ${CMAKE_SOURCE_DIR}/src/Altirra/h
)

if(NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

target_link_libraries(AltirraSDL PRIVATE
    # Core emulation libraries
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

    # SDL3
    SDL3::SDL3

    # Dear ImGui
    imgui
)

# OpenGL: desktop GL on Linux/macOS/Windows, GLESv3 on Android
if(ANDROID)
    target_link_libraries(AltirraSDL PRIVATE GLESv3 EGL android log)
else()
    target_link_libraries(AltirraSDL PRIVATE OpenGL::GL)
endif()

# Platform-specific link libraries
# Note: winmm and ws2_32 propagate transitively from system and ATNetworkSockets.
if(APPLE)
    target_link_libraries(AltirraSDL PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()

# Platform compile definitions (match what the top-level sets globally, but
# AltirraSDL adds its own private copy because it also gets the global one —
# this is harmless and ensures correctness if the target is ever split out)
if(WIN32)
    target_compile_definitions(AltirraSDL PRIVATE VD_OS_WINDOWS=1)
    # Force-include POSIX compat header for strcasestr etc. (MSVC only).
    if(ALTIRRA_POSIX_COMPAT_HEADER)
        target_compile_options(AltirraSDL PRIVATE /FI${ALTIRRA_POSIX_COMPAT_HEADER})
    endif()
elseif(ANDROID)
    target_compile_definitions(AltirraSDL PRIVATE VD_OS_ANDROID=1 ALTIRRA_MOBILE=1)
elseif(APPLE)
    target_compile_definitions(AltirraSDL PRIVATE VD_OS_MACOS=1)
    # Silence Apple's blanket OpenGL deprecation warnings (entire API deprecated
    # in macOS 10.14, but still functional and the only GL path available here).
    target_compile_definitions(AltirraSDL PRIVATE GL_SILENCE_DEPRECATION=1)
    # Suppress Clang's "unknown attribute" noise for the MSVC-specific
    # [[msvc::no_unique_address]] annotation in vdalloc.h.
    target_compile_options(AltirraSDL PRIVATE -Wno-unknown-attributes)
elseif(UNIX)
    target_compile_definitions(AltirraSDL PRIVATE VD_OS_LINUX=1)
endif()

# ===========================================================================
# Install rules — creates a self-contained distribution folder.
#
# Usage:
#   cmake --install <builddir> --prefix <outdir>
#
# Produces:
#   <outdir>/
#   ├── AltirraSDL                  (binary)
#   └── extras/                     (custom effects, sample devices, etc.)
#       ├── customeffects/
#       ├── sampledevices/
#       ├── deviceserver/
#       └── readme.txt
#
# The binary and extras are co-located in the same folder — matches the
# Windows Altirra distribution layout (zip → extract → run).
# ===========================================================================

include(GNUInstallDirs)

install(TARGETS AltirraSDL
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# Extras — custom effects, sample devices, device server scripts
if(EXISTS "${CMAKE_SOURCE_DIR}/dist/extras")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/dist/extras/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/altirra/extras"
    )
endif()

# ===========================================================================
# Package target — builds a portable distribution folder.
#
# Usage:
#   cmake --build <builddir> --target package_altirra
#
# Produces: <builddir>/AltirraSDL-<version>/
#           ready to zip/tar and distribute.
# ===========================================================================

set(ALTIRRA_PKG_DIR "${CMAKE_BINARY_DIR}/AltirraSDL-${PROJECT_VERSION}")

add_custom_target(package_altirra
    DEPENDS AltirraSDL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ALTIRRA_PKG_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AltirraSDL>" "${ALTIRRA_PKG_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/dist/extras"
        "${ALTIRRA_PKG_DIR}/extras"
    # Include license file (matches Windows Altirra distribution)
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/Copying"
        "${ALTIRRA_PKG_DIR}/Copying"
    COMMENT "Packaging AltirraSDL ${PROJECT_VERSION} -> ${ALTIRRA_PKG_DIR}"
)

# Bundle SDL3 shared library into the package.
# On Windows this is always needed.  On Linux/macOS it's needed when SDL3
# was built via FetchContent (not a system package).
if(TARGET SDL3::SDL3-shared)
    if(WIN32)
        # Always bundle on Windows — there's no system SDL3.
        set(_BUNDLE_SDL3 TRUE)
    elseif(sdl3_POPULATED)
        # FetchContent build — the .so/.dylib won't be in a system path.
        set(_BUNDLE_SDL3 TRUE)
    else()
        # System-installed SDL3 — don't bundle, it's managed by the package manager.
        set(_BUNDLE_SDL3 FALSE)
    endif()

    if(_BUNDLE_SDL3)
        add_custom_command(TARGET package_altirra POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "${ALTIRRA_PKG_DIR}/"
            COMMENT "Bundling $<TARGET_FILE_NAME:SDL3::SDL3-shared> into package"
        )
    endif()
endif()

# Bundle librashader shared library if present next to the executable.
# The build.sh --librashader step places it there; we just copy it into
# the package directory.  Use a generator expression for the exe directory
# so it resolves correctly on all generators (Makefiles vs MSVC).
if(ENABLE_LIBRASHADER)
    if(WIN32)
        set(_LIBRA_NAME "librashader.dll")
    elseif(APPLE)
        set(_LIBRA_NAME "librashader.dylib")
    else()
        set(_LIBRA_NAME "librashader.so")
    endif()

    # Look next to the executable (works for both single-config and
    # multi-config generators like MSVC which add Release/ etc.)
    set(_LIBRA_PATH "$<TARGET_FILE_DIR:AltirraSDL>/${_LIBRA_NAME}")

    # Use a script-mode cmake snippet (copy_if_exists.cmake) so
    # packaging never fails just because librashader wasn't built.
    add_custom_command(TARGET package_altirra POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC="${_LIBRA_PATH}"
            -DDST="${ALTIRRA_PKG_DIR}/"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMENT "Bundling ${_LIBRA_NAME} into package (if present)"
    )
endif()

# Per-file SIMD flags for GCC/Clang (MSVC handles this via /arch: flags)
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
    # antic.cpp includes antic_sse2.inl which uses _tzcnt_u32 (BMI1)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/Altirra/source/antic.cpp"
        PROPERTIES COMPILE_FLAGS "-mbmi"
    )
    # tracefileencoding.cpp uses SSSE3 + popcnt intrinsics
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/src/Altirra/source/tracefileencoding.cpp"
        PROPERTIES COMPILE_FLAGS "-mssse3 -mpopcnt"
    )
endif()
