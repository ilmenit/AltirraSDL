# AltirraLibretro — RetroArch/libretro shared-library adapter.
#
# This target owns the libretro adapter sources and links the existing
# emulator/static-library graph without changing AltirraSDL or BridgeServer.

add_library(AltirraLibretro SHARED
    libretro.cpp
)

set(ALTIRRA_SDL_DIR ${CMAKE_SOURCE_DIR}/src/AltirraSDL)

include(${ALTIRRA_SDL_DIR}/cmake/altirra_core_sources.cmake)

target_sources(AltirraLibretro PRIVATE
    libretro_audio.cpp
    libretro_dirs.cpp
    libretro_time.cpp
    libretro_video.cpp
    libretro_ui_stubs.cpp

    ${ALTIRRA_ALL_SOURCES}

    ${ALTIRRA_SDL_DIR}/stubs/console_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/oshelper_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/uiaccessors_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/win32_stubs.cpp
    ${ALTIRRA_SDL_DIR}/stubs/device_stubs.cpp

    ${ALTIRRA_SDL_DIR}/source/os/directorywatcher_sdl3.cpp
    libretro_midimate.cpp
    ${ALTIRRA_SDL_DIR}/source/input/keyboard_keymap_sdl3.cpp

    ${CMAKE_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/Altirra/source/savestateio.cpp
    ${CMAKE_SOURCE_DIR}/src/ATUI/source/uicommandmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/Dita/source/accel.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/screenfx.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/bloom.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/bicubic.cpp
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/source/displaytypes.cpp
)

if(WIN32)
    target_sources(AltirraLibretro PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Altirra/source/pclink_win32.cpp
    )
endif()

if(NOT WIN32)
    target_sources(AltirraLibretro PRIVATE
        ${ALTIRRA_SDL_DIR}/source/os/modemtcp_sdl3.cpp
        ${ALTIRRA_SDL_DIR}/source/os/idephysdisk_sdl3.cpp
        ${ALTIRRA_SDL_DIR}/source/os/pipeserial_sdl3.cpp
    )
endif()

set_target_properties(AltirraLibretro PROPERTIES
    OUTPUT_NAME "altirra_libretro"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

add_custom_command(TARGET AltirraLibretro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/altirra_libretro.info"
        "$<TARGET_FILE_DIR:AltirraLibretro>/altirra_libretro.info"
    COMMENT "Copying Altirra libretro core info"
)

target_compile_definitions(AltirraLibretro PRIVATE
    ALTIRRA_LIBRETRO=1
    AT_SDL3_PORTABLE=1
)

if(ALTIRRA_LIBRETRO_NO_SDL3)
    target_compile_definitions(AltirraLibretro PRIVATE
        ALTIRRA_LIBRETRO_NO_SDL3=1)
endif()

target_include_directories(AltirraLibretro PRIVATE
    ${ALTIRRA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Altirra/h
    ${CMAKE_SOURCE_DIR}/src/Altirra/source
    ${ALTIRRA_SDL_DIR}/source
    ${ALTIRRA_SDL_DIR}/source/app
    ${ALTIRRA_SDL_DIR}/source/ui/core
    ${CMAKE_SOURCE_DIR}/src/ATAudio/h
    ${CMAKE_SOURCE_DIR}/src/VDDisplay/h
    ${CMAKE_SOURCE_DIR}/src/Altirra/res
    ${CMAKE_SOURCE_DIR}/src/Altirra/autogen
    ${CMAKE_SOURCE_DIR}/src/Altirra/autobuild_default
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(AltirraLibretro PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_include_directories(AltirraLibretro BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AltirraBridgeServer/stubs
    ${ALTIRRA_SDL_DIR}/stubs
    ${CMAKE_SOURCE_DIR}/src/Altirra/h
)

target_link_libraries(AltirraLibretro PRIVATE
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

if(WIN32)
    target_compile_definitions(AltirraLibretro PRIVATE VD_OS_WINDOWS=1)
elseif(ANDROID)
    target_compile_definitions(AltirraLibretro PRIVATE VD_OS_ANDROID=1)
elseif(APPLE)
    target_compile_definitions(AltirraLibretro PRIVATE VD_OS_MACOS=1)
elseif(UNIX)
    target_compile_definitions(AltirraLibretro PRIVATE VD_OS_LINUX=1)
endif()

if(MSVC)
    # Windows SDK 10.0.26100 UCRT headers can reference SIMD intrinsic types
    # before they are declared when building the CMake/Ninja target. Force the
    # declarations in first, matching the headless build workaround.
    target_compile_options(AltirraLibretro PRIVATE /FIintrin.h)
endif()

if(UNIX AND NOT APPLE)
    target_link_options(AltirraLibretro PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/altirra_libretro.map"
        "LINKER:--as-needed"
        "LINKER:--exclude-libs,ALL"
    )
elseif(APPLE)
    target_link_options(AltirraLibretro PRIVATE
        "LINKER:-dead_strip_dylibs"
        "LINKER:-exported_symbol,_retro_api_version"
        "LINKER:-exported_symbol,_retro_cheat_reset"
        "LINKER:-exported_symbol,_retro_cheat_set"
        "LINKER:-exported_symbol,_retro_deinit"
        "LINKER:-exported_symbol,_retro_get_memory_data"
        "LINKER:-exported_symbol,_retro_get_memory_size"
        "LINKER:-exported_symbol,_retro_get_region"
        "LINKER:-exported_symbol,_retro_get_system_av_info"
        "LINKER:-exported_symbol,_retro_get_system_info"
        "LINKER:-exported_symbol,_retro_init"
        "LINKER:-exported_symbol,_retro_load_game"
        "LINKER:-exported_symbol,_retro_load_game_special"
        "LINKER:-exported_symbol,_retro_reset"
        "LINKER:-exported_symbol,_retro_run"
        "LINKER:-exported_symbol,_retro_serialize"
        "LINKER:-exported_symbol,_retro_serialize_size"
        "LINKER:-exported_symbol,_retro_set_audio_sample"
        "LINKER:-exported_symbol,_retro_set_audio_sample_batch"
        "LINKER:-exported_symbol,_retro_set_controller_port_device"
        "LINKER:-exported_symbol,_retro_set_environment"
        "LINKER:-exported_symbol,_retro_set_input_poll"
        "LINKER:-exported_symbol,_retro_set_input_state"
        "LINKER:-exported_symbol,_retro_set_video_refresh"
        "LINKER:-exported_symbol,_retro_unload_game"
        "LINKER:-exported_symbol,_retro_unserialize"
    )
endif()

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

install(TARGETS AltirraLibretro
    LIBRARY DESTINATION lib/libretro
    RUNTIME DESTINATION lib/libretro
)
install(FILES altirra_libretro.info
    DESTINATION share/libretro/info
)
