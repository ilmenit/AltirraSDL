cmake_minimum_required(VERSION 3.24)

# Computes ALTIRRA_ALL_SOURCES — the set of platform-agnostic Altirra
# emulation source files with Win32 UI / unused-files filtered out.
# Shared between AltirraSDL and AltirraBridgeServer; see the helper
# file for the full filter list and rationale.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/altirra_core_sources.cmake)
# Re-publish for the parent scope so add_subdirectory(AltirraBridgeServer)
# can read the same list without re-globbing.
set(ALTIRRA_ALL_SOURCES "${ALTIRRA_ALL_SOURCES}" PARENT_SCOPE)

# On Android, the native code is a shared library loaded by SDLActivity.
# On desktop, it's a regular executable.
if(ANDROID)
    add_library(AltirraSDL SHARED)
    # Android APK expects the library to be named "libmain.so"
    set_target_properties(AltirraSDL PROPERTIES OUTPUT_NAME "main")
elseif(APPLE)
    # macOS: build as a real .app bundle.  This is REQUIRED for SDL3
    # gamepad/joystick support — Apple's GameController.framework refuses
    # to enumerate controllers (USB, Bluetooth, Xbox, DualShock, MFi)
    # for plain Mach-O CLI binaries on macOS 14+, so a bare executable
    # would silently see zero gamepads.  See macos/Info.plist.in for the
    # NSGameControllerUsageDescription declaration.
    add_executable(AltirraSDL MACOSX_BUNDLE)
    set_target_properties(AltirraSDL PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME            "Altirra"
        MACOSX_BUNDLE_GUI_IDENTIFIER         "org.virtualdub.altirra"
        MACOSX_BUNDLE_BUNDLE_VERSION         "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING   "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST             "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
    )
else()
    add_executable(AltirraSDL)
endif()

target_sources(AltirraSDL PRIVATE
    # --- app/ : process lifecycle, main loop, CLI, crash ---
    source/app/main_sdl3.cpp
    source/app/main_pacer.cpp
    source/app/main_window.cpp
    source/app/window_caption_sdl3.cpp
    source/app/main_android_bridge.cpp
    source/app/commandline_sdl3.cpp
    source/app/crash_report.cpp
    source/app/android_platform.cpp
    source/app/services_sdl3.cpp
    source/app/ui_file_dialog_sdl3.cpp

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
    source/input/keyboard_keymap_sdl3.cpp
    source/input/touch_layout.cpp
    source/input/touch_controls.cpp
    source/input/touch_widgets.cpp

    # --- media/ : video recording, accelerator table ---
    source/media/videowriter_sdl3.cpp
    source/media/accel_sdl3.cpp
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/aviwriter.cpp

    # --- ui/core/ : ImGui hub, main render, indicators ---
    source/ui/core/ui_main.cpp
    source/ui/core/ui_mode.cpp
    source/ui/core/ui_main_aboutdialogs.cpp
    source/ui/core/ui_main_compat.cpp
    source/ui/core/ui_main_overlays.cpp
    source/ui/core/ui_confirm_dialog.cpp
    source/ui/core/ui_indicators.cpp
    source/ui/core/ui_fonts.cpp

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
    source/ui/ui_virtual_keyboard.cpp

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
    source/ui/debugger/ui_dbg_printer_png.cpp
    source/ui/debugger/ui_dbg_printer_pdf_svg.cpp
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
    source/ui/mobile/mobile_visual_effects.cpp
    source/ui/mobile/mobile_suspend.cpp
    source/ui/mobile/mobile_dialogs.cpp
    source/ui/mobile/mobile_settings_firmware.cpp
    source/ui/mobile/mobile_gamepad.cpp
    source/ui/mobile/mobile_game_browser.cpp

    # --- ui/gamelibrary/ : Game Library (data model, scanner, cache) ---
    source/ui/gamelibrary/game_library.cpp
    source/ui/gamelibrary/game_library_art.cpp

    # Registry persistence (filtered out by ui* regex, so add explicitly)
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp

    # ATUICommandManager (from ATUI library, no Win32 deps — needed by debuggerautotest.cpp)
    ${CMAKE_SOURCE_DIR}/src/ATUI/source/uicommandmanager.cpp

    # VDAccelTableDefinition (from Dita library, no Win32 deps — accelerator tables)
    ${CMAKE_SOURCE_DIR}/src/Dita/source/accel.cpp

    # SDL3 command handlers (accel_sdl3.cpp already listed in media/ group)
    source/commands_sdl3.cpp

    # Generated embedded font blobs (see "Embedded fonts" section below)
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_font_roboto.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_font_firamono.cpp

    # Portable oshelper.h implementations (replaces Windows oshelper.cpp)
    source/os/oshelper_sdl3.cpp

    # Stub implementations replacing Win32-only files
    stubs/console_stubs.cpp
    # oshelper_stubs.cpp is used only by the headless bridge server target;
    # the AltirraSDL GUI target uses source/os/oshelper_sdl3.cpp instead.
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

# AltirraBridge — scripting/automation server (TCP/UDS, line-delimited JSON).
# Self-contained module under source/bridge/. Default ON. Disable with
# -DALTIRRA_BRIDGE=OFF for size-sensitive builds; the rest of AltirraSDL
# compiles unchanged and the binary contains no bridge symbols.
# See src/AltirraSDL/AltirraBridge/docs/PROTOCOL.md for the wire contract.
# Winsock (ws2_32) propagates transitively from `system` and ATNetworkSockets,
# so no explicit link is needed on Windows.
option(ALTIRRA_BRIDGE "Build the AltirraBridge scripting server" ON)
if(ALTIRRA_BRIDGE)
    target_sources(AltirraSDL PRIVATE
        source/bridge/bridge_server.cpp
        source/bridge/bridge_protocol.cpp
        source/bridge/bridge_transport.cpp
        source/bridge/bridge_commands_state.cpp
        source/bridge/bridge_commands_write.cpp
        source/bridge/bridge_commands_render.cpp
        source/bridge/bridge_commands_debug.cpp
        source/bridge/bridge_commands_debug2.cpp
        source/bridge/bridge_bare_stub.cpp
        # Per-target glue: SDL3 routes BOOT/MOUNT/STATE_* through
        # the deferred-action queue. The headless AltirraBridgeServer
        # target provides its own implementation of these symbols.
        source/bridge/bridge_main_glue_sdl3.cpp
    )
    target_compile_definitions(AltirraSDL PRIVATE ALTIRRA_BRIDGE_ENABLED=1)
endif()

# ---------------------------------------------------------------------------
# Window/taskbar icon
# ---------------------------------------------------------------------------
# Bake the Android launcher PNGs into:
#   - altirra_icon_data.{h,c}  — RGBA arrays consumed by main_sdl3.cpp to
#                                call SDL_SetWindowIcon (covers Linux
#                                taskbar, macOS dock-while-running,
#                                Windows caption).
#   - altirra.ico              — PNG-embedded .ico referenced by the
#                                Windows resource script for the exe /
#                                taskbar icon (Explorer uses the embedded
#                                .ico, not SDL_SetWindowIcon).
#   - altirra.icns             — Apple IconSuite bundled into
#                                AltirraSDL.app/Contents/Resources so
#                                Finder, Launchpad, and the Dock-before-
#                                launch all show the Altirra icon.  The
#                                runtime SDL_SetWindowIcon path only
#                                covers the dock tile AFTER launch; the
#                                .icns is what replaces the generic
#                                placeholder everywhere else.
#
# The script uses Python stdlib only (zlib), so it runs on every
# GitHub-hosted runner without pip installs.  Not built on Android: the
# APK already has launcher icons under android/app/src/main/res/mipmap-*.
if(NOT ANDROID)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(ALTIRRA_ICON_SRC_PNGS
        ${CMAKE_SOURCE_DIR}/android/app/src/main/res/mipmap-mdpi/ic_launcher.png
        ${CMAKE_SOURCE_DIR}/android/app/src/main/res/mipmap-hdpi/ic_launcher.png
        ${CMAKE_SOURCE_DIR}/android/app/src/main/res/mipmap-xhdpi/ic_launcher.png
        ${CMAKE_SOURCE_DIR}/android/app/src/main/res/mipmap-xxhdpi/ic_launcher.png
        ${CMAKE_SOURCE_DIR}/android/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png
    )
    set(ALTIRRA_ICON_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(ALTIRRA_ICON_GEN_H ${ALTIRRA_ICON_GEN_DIR}/altirra_icon_data.h)
    set(ALTIRRA_ICON_GEN_C ${ALTIRRA_ICON_GEN_DIR}/altirra_icon_data.cpp)
    set(ALTIRRA_ICON_GEN_ICO ${ALTIRRA_ICON_GEN_DIR}/altirra.ico)
    set(ALTIRRA_ICON_GEN_ICNS ${ALTIRRA_ICON_GEN_DIR}/altirra.icns)
    set(ALTIRRA_ICON_BAKE_SCRIPT
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_icon.py)

    add_custom_command(
        OUTPUT ${ALTIRRA_ICON_GEN_H} ${ALTIRRA_ICON_GEN_C}
               ${ALTIRRA_ICON_GEN_ICO} ${ALTIRRA_ICON_GEN_ICNS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ALTIRRA_ICON_GEN_DIR}
        COMMAND ${Python3_EXECUTABLE} ${ALTIRRA_ICON_BAKE_SCRIPT}
                ${ALTIRRA_ICON_GEN_DIR} ${ALTIRRA_ICON_SRC_PNGS}
        DEPENDS ${ALTIRRA_ICON_BAKE_SCRIPT} ${ALTIRRA_ICON_SRC_PNGS}
        COMMENT "Baking Altirra window icon (PNG -> RGBA + .ico + .icns)"
        VERBATIM
    )

    target_sources(AltirraSDL PRIVATE ${ALTIRRA_ICON_GEN_C})
    target_include_directories(AltirraSDL PRIVATE ${ALTIRRA_ICON_GEN_DIR})
    target_compile_definitions(AltirraSDL PRIVATE ALTIRRA_HAS_BAKED_ICON=1)

    if(WIN32)
        # AltirraSDL.rc references altirra.ico via ALTIRRA_ICON_PATH
        # (absolute path into the build tree, forward-slashed so the
        # resource compiler is happy on all generators).
        set(ALTIRRA_ICON_RC
            ${CMAKE_CURRENT_BINARY_DIR}/AltirraSDL_icon.rc)
        file(WRITE ${ALTIRRA_ICON_RC}
            "// Auto-generated — do not edit.\n"
            "IDI_ICON1 ICON \"${ALTIRRA_ICON_GEN_ICO}\"\n")
        # Mark the .rc as depending on the generated .ico so MSVC/MinGW
        # re-run rc.exe when the icon changes.
        set_source_files_properties(${ALTIRRA_ICON_RC}
            PROPERTIES OBJECT_DEPENDS ${ALTIRRA_ICON_GEN_ICO})
        target_sources(AltirraSDL PRIVATE ${ALTIRRA_ICON_RC})
    endif()

    if(APPLE)
        # Attach the generated .icns to the .app bundle.  CMake will
        # copy it to AltirraSDL.app/Contents/Resources/altirra.icns at
        # build time, and substitute ${MACOSX_BUNDLE_ICON_FILE} into
        # Info.plist.in so CFBundleIconFile points at it.
        #
        # MACOSX_PACKAGE_LOCATION both (a) tells CMake where inside the
        # bundle to drop the file and (b) tells it NOT to try to
        # compile it — without this property an unknown extension like
        # .icns would reach the compiler and fail.
        set_source_files_properties(${ALTIRRA_ICON_GEN_ICNS}
            PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
                GENERATED TRUE)
        target_sources(AltirraSDL PRIVATE ${ALTIRRA_ICON_GEN_ICNS})
        set_target_properties(AltirraSDL PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "altirra.icns")
    endif()
endif()

# ---------------------------------------------------------------------------
# Virtual keyboard image
# ---------------------------------------------------------------------------
# Bake the keyboard BMP + key region JSON into C++ data arrays.
# The script uses Python stdlib only (json, struct).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(KEYBOARD_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/keyboard)
set(KEYBOARD_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(KEYBOARD_GEN_H ${KEYBOARD_GEN_DIR}/keyboard_data.h)
set(KEYBOARD_GEN_C ${KEYBOARD_GEN_DIR}/keyboard_data.cpp)
set(KEYBOARD_BAKE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_keyboard.py)

add_custom_command(
    OUTPUT ${KEYBOARD_GEN_H} ${KEYBOARD_GEN_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${KEYBOARD_GEN_DIR}
    COMMAND ${Python3_EXECUTABLE} ${KEYBOARD_BAKE_SCRIPT}
            ${KEYBOARD_GEN_DIR}
            ${KEYBOARD_ASSETS_DIR}/keyboard.bmp
            ${KEYBOARD_ASSETS_DIR}/rescaledkeys.json
    DEPENDS ${KEYBOARD_BAKE_SCRIPT}
            ${KEYBOARD_ASSETS_DIR}/keyboard.bmp
            ${KEYBOARD_ASSETS_DIR}/rescaledkeys.json
    COMMENT "Baking virtual keyboard data (BMP + JSON -> C++)"
    VERBATIM
)

target_sources(AltirraSDL PRIVATE ${KEYBOARD_GEN_C})
target_include_directories(AltirraSDL PRIVATE ${KEYBOARD_GEN_DIR})

# ===========================================================================
# Build info header — stamps version, build date/time, and git commit into
# a generated header.  Runs at BUILD time (not configure time) so the
# timestamp reflects the actual build.  The header is regenerated on every
# build so the date stays current for nightly builds.
# ===========================================================================
set(BUILDINFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/buildinfo")
set(BUILDINFO_HEADER "${BUILDINFO_DIR}/buildinfo.h")

add_custom_target(generate_buildinfo ALL
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_FILE=${BUILDINFO_HEADER}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DPROJECT_VER=${PROJECT_VERSION}
        -P ${CMAKE_SOURCE_DIR}/cmake/generate_buildinfo.cmake
    COMMENT "Generating build info header (version, date, git commit)"
    VERBATIM
)
add_dependencies(AltirraSDL generate_buildinfo)
target_include_directories(AltirraSDL PRIVATE ${BUILDINFO_DIR})

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
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/ui
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
    ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/bridge
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

# SDL3_image is optional — ALTIRRA_SKIP_SDL3_IMAGE disables it entirely.
# oshelper_sdl3.cpp compiles stub image entry points in that case.
if(NOT ALTIRRA_SKIP_SDL3_IMAGE)
    target_link_libraries(AltirraSDL PRIVATE SDL3_image::SDL3_image)
endif()

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

# Make the executable find a co-located libSDL3.so / libSDL3.dylib at runtime,
# so the bundled package (see package_altirra below) is self-contained and
# users don't need a system-installed SDL3.
if(UNIX AND NOT APPLE)
    set_target_properties(AltirraSDL PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN")
elseif(APPLE)
    set_target_properties(AltirraSDL PROPERTIES
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path")
endif()

install(TARGETS AltirraSDL
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# ===========================================================================
# Embedded fonts
# ===========================================================================
# Bake the default UI font (Roboto Medium) and monospace font (Fira Mono
# Regular) into the binary as C arrays.  This guarantees the UI works on
# any platform without requiring a fonts/ directory at runtime — Android
# assets, sandboxed installs, packaged tarballs, and CI all behave the
# same.  Users can still drop additional .ttf files into <exe>/fonts/ to
# expand the family list shown in Configure System -> Fonts.
#
# The conversion is driven by cmake/embed_font.cmake (a -P script), so no
# host build tool is needed and cross-compiling (Android NDK) just works.
add_custom_command(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/embedded_font_roboto.cpp"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto/static/Roboto-Medium.ttf
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded_font_roboto.cpp
        -DSYMBOL=g_atFontRobotoMediumData
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_font.cmake
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto/static/Roboto-Medium.ttf
        ${CMAKE_SOURCE_DIR}/cmake/embed_font.cmake
    VERBATIM
    COMMENT "Embedding Roboto-Medium.ttf"
)
add_custom_command(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/embedded_font_firamono.cpp"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fonts/Fira_Mono/FiraMono-Regular.ttf
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded_font_firamono.cpp
        -DSYMBOL=g_atFontFiraMonoRegularData
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_font.cmake
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Fira_Mono/FiraMono-Regular.ttf
        ${CMAKE_SOURCE_DIR}/cmake/embed_font.cmake
    VERBATIM
    COMMENT "Embedding FiraMono-Regular.ttf"
)

# Stage bundled TTF/OTF fonts next to the binary so the Fonts page in
# Configure System (and the default Roboto/Fira Mono load on first run)
# can find them via SDL_GetBasePath() + "fonts/".
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fonts")
    add_custom_command(TARGET AltirraSDL POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "$<TARGET_FILE_DIR:AltirraSDL>/fonts"
        COMMENT "Staging bundled fonts next to AltirraSDL"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fonts/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/fonts"
    )
endif()

# Copy the SDL3 shared library next to AltirraSDL after the build, so
# the binary can be run straight out of the build tree (and so any IDE
# launches "just work") without needing a system SDL3 install.  The
# package_altirra target below also copies it into the distribution dir.
if(TARGET SDL3::SDL3-shared AND (sdl3_POPULATED OR WIN32))
    add_custom_command(TARGET AltirraSDL POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3-shared>"
            "$<TARGET_FILE_DIR:AltirraSDL>/"
        COMMENT "Staging $<TARGET_FILE_NAME:SDL3::SDL3-shared> next to AltirraSDL"
    )
    # Also copy the SONAME symlink on Linux (libSDL3.so.0), since the ELF
    # NEEDED entry refers to the soname, not the real filename.
    if(UNIX AND NOT APPLE)
        add_custom_command(TARGET AltirraSDL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_SONAME_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:AltirraSDL>/"
        )
    endif()
endif()

# Stage SDL3_image shared library next to AltirraSDL (same rationale as SDL3).
# On macOS SDL3_image is built with both VERSION and SOVERSION, so
# TARGET_FILE is libSDL3_image.0.2.4.dylib (real file) and
# TARGET_SONAME_FILE is libSDL3_image.0.dylib (the symlink dyld resolves
# against the executable's LC_LOAD_DYLIB).  We must copy the SONAME file
# too, otherwise the .app bundle is missing the name dyld looks up and
# the binary crashes on launch with "Library not loaded".
if(NOT ALTIRRA_SKIP_SDL3_IMAGE AND TARGET SDL3_image::SDL3_image-shared AND (sdl3_image_POPULATED OR WIN32))
    add_custom_command(TARGET AltirraSDL POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3_image::SDL3_image-shared>"
            "$<TARGET_FILE_DIR:AltirraSDL>/"
        COMMENT "Staging $<TARGET_FILE_NAME:SDL3_image::SDL3_image-shared> next to AltirraSDL"
    )
    if(UNIX)
        add_custom_command(TARGET AltirraSDL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_SONAME_FILE:SDL3_image::SDL3_image-shared>"
                "$<TARGET_FILE_DIR:AltirraSDL>/"
        )
    endif()
endif()

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

if(APPLE)
    # On macOS the build product is an .app bundle directory; copy the
    # whole bundle into the package, not just the inner Mach-O binary.
    add_custom_target(package_altirra
        DEPENDS AltirraSDL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ALTIRRA_PKG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:AltirraSDL>"
            "${ALTIRRA_PKG_DIR}/AltirraSDL.app"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/dist/extras"
            "${ALTIRRA_PKG_DIR}/extras"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/Copying"
            "${ALTIRRA_PKG_DIR}/Copying"
        COMMENT "Packaging AltirraSDL.app ${PROJECT_VERSION} -> ${ALTIRRA_PKG_DIR}"
    )
else()
    add_custom_target(package_altirra
        DEPENDS AltirraSDL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ALTIRRA_PKG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:AltirraSDL>" "${ALTIRRA_PKG_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "${ALTIRRA_PKG_DIR}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/dist/extras"
            "${ALTIRRA_PKG_DIR}/extras"
        # Include license file (matches Windows Altirra distribution)
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/Copying"
            "${ALTIRRA_PKG_DIR}/Copying"
        COMMENT "Packaging AltirraSDL ${PROJECT_VERSION} -> ${ALTIRRA_PKG_DIR}"
    )
endif()

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
        if(APPLE)
            # Inside the .app bundle so @executable_path resolves at runtime.
            set(_SDL3_DEST "${ALTIRRA_PKG_DIR}/AltirraSDL.app/Contents/MacOS/")
        else()
            set(_SDL3_DEST "${ALTIRRA_PKG_DIR}/")
        endif()
        add_custom_command(TARGET package_altirra POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "${_SDL3_DEST}"
            COMMENT "Bundling $<TARGET_FILE_NAME:SDL3::SDL3-shared> into package"
        )
        # On Linux the binary's NEEDED entry is the soname (libSDL3.so.0),
        # not the real filename — copy the soname symlink too.
        if(UNIX AND NOT APPLE)
            add_custom_command(TARGET package_altirra POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                    "$<TARGET_SONAME_FILE:SDL3::SDL3-shared>"
                    "${ALTIRRA_PKG_DIR}/"
            )
        endif()
    endif()
endif()

# Bundle SDL3_image shared library into the package (same logic as SDL3).
if(NOT ALTIRRA_SKIP_SDL3_IMAGE AND TARGET SDL3_image::SDL3_image-shared)
    if(WIN32)
        set(_BUNDLE_SDL3_IMAGE TRUE)
    elseif(sdl3_image_POPULATED)
        set(_BUNDLE_SDL3_IMAGE TRUE)
    else()
        set(_BUNDLE_SDL3_IMAGE FALSE)
    endif()

    if(_BUNDLE_SDL3_IMAGE)
        if(APPLE)
            set(_SDL3_IMAGE_DEST "${ALTIRRA_PKG_DIR}/AltirraSDL.app/Contents/MacOS/")
        else()
            set(_SDL3_IMAGE_DEST "${ALTIRRA_PKG_DIR}/")
        endif()
        add_custom_command(TARGET package_altirra POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "$<TARGET_FILE:SDL3_image::SDL3_image-shared>"
                "${_SDL3_IMAGE_DEST}"
            COMMENT "Bundling $<TARGET_FILE_NAME:SDL3_image::SDL3_image-shared> into package"
        )
        # SONAME symlink is needed on both Linux (ELF NEEDED uses soname)
        # and macOS (LC_LOAD_DYLIB uses install_name @rpath/libSDL3_image.0.dylib).
        if(UNIX)
            if(APPLE)
                set(_SDL3_IMAGE_SONAME_DEST "${_SDL3_IMAGE_DEST}")
            else()
                set(_SDL3_IMAGE_SONAME_DEST "${ALTIRRA_PKG_DIR}/")
            endif()
            add_custom_command(TARGET package_altirra POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                    "$<TARGET_SONAME_FILE:SDL3_image::SDL3_image-shared>"
                    "${_SDL3_IMAGE_SONAME_DEST}"
            )
        endif()
    endif()
endif()

# Bundle librashader shared library if present next to the executable.
# The build.sh --librashader step places it there; we just copy it into
# the package directory.  Use a generator expression for the exe directory
# so it resolves correctly on all generators (Makefiles vs MSVC).
if(ENABLE_LIBRASHADER)
    if(WIN32)
        set(_LIBRA_GLOB "librashader.dll")
        set(_LIBRA_DST "${ALTIRRA_PKG_DIR}/")
    elseif(APPLE)
        set(_LIBRA_GLOB "librashader*.dylib")
        # Inside the .app bundle so @executable_path resolves at runtime
        # and the dylib is covered by the ad-hoc codesign step below.
        set(_LIBRA_DST "${ALTIRRA_PKG_DIR}/AltirraSDL.app/Contents/MacOS/")
    else()
        # Match librashader.so, librashader.so.1, librashader.so.2, etc.
        # The loader in vendor/librashader/librashader_ld.h tries all of
        # these in order; we need to ship whichever one is present.
        set(_LIBRA_GLOB "librashader.so*")
        set(_LIBRA_DST "${ALTIRRA_PKG_DIR}/")
    endif()

    # Look next to the executable (works for both single-config and
    # multi-config generators like MSVC which add Release/ etc.).  On
    # macOS this resolves to AltirraSDL.app/Contents/MacOS/, which is
    # also where librashader.sh now writes the dylib.
    set(_LIBRA_GLOB_PATH "$<TARGET_FILE_DIR:AltirraSDL>/${_LIBRA_GLOB}")

    # Use a script-mode cmake snippet (copy_if_exists.cmake) so
    # packaging never fails just because librashader wasn't built.
    add_custom_command(TARGET package_altirra POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC_GLOB=${_LIBRA_GLOB_PATH}
            -DDST=${_LIBRA_DST}
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMENT "Bundling ${_LIBRA_GLOB} into package (if present)"
    )
endif()

# macOS: ad-hoc codesign the packaged .app bundle as the FINAL POST_BUILD
# step, so any dylibs (SDL3, librashader) added by earlier POST_BUILD
# commands are included in the signature.  macOS 14+ refuses to grant
# GameController access to an unsigned bundle even with the
# NSGameControllerUsageDescription key present; ad-hoc signing ("-")
# satisfies that check and requires no Developer ID.
if(APPLE)
    add_custom_command(TARGET package_altirra POST_BUILD
        COMMAND codesign --force --deep --sign -
            "${ALTIRRA_PKG_DIR}/AltirraSDL.app"
        COMMENT "Ad-hoc codesigning AltirraSDL.app"
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
