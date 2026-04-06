set(SYSTEM_COMMON_SOURCES
    source/binary.cpp
    source/bitmath.cpp
    source/cache.cpp
    source/cmdline.cpp
    source/constexpr.cpp
    source/cpuaccel.cpp
    source/Error.cpp
    source/event.cpp
    source/filestream.cpp
    source/Fraction.cpp
    source/function.cpp
    source/halffloat.cpp
    source/hash.cpp
    source/int128.cpp
    source/linearalloc.cpp
    source/math.cpp
    source/refcount.cpp
    source/registrymemory.cpp
    source/memory.cpp
    source/stdaccel.cpp
    source/stdafx.cpp
    source/strutil.cpp
    source/thread.cpp
    source/tls.cpp
    source/vdalloc.cpp
    source/vdstl.cpp
    source/vdstl_hash.cpp
    source/vdstl_hashtable.cpp
    source/VDString.cpp
    source/vectors.cpp
    source/zip.cpp
)

if(WIN32)
    set(SYSTEM_PLATFORM_SOURCES
        # Note: cpuaccel.cpp, event.cpp, filestream.cpp, math.cpp, zip.cpp are
        # already in SYSTEM_COMMON_SOURCES and must not be listed again here.
        source/date.cpp
        source/debug.cpp
        source/debugx86.cpp
        source/error_win32.cpp
        source/file.cpp
        source/fileasync.cpp
        source/filesys.cpp
        source/filewatcher.cpp
        source/process.cpp
        source/registry.cpp
        source/text.cpp
        source/thunk.cpp
        source/time.cpp
        source/w32assist.cpp
    )
    if(ALTIRRA_SDL3)
        # SDL3 frontend on Windows needs ATGetConfigDir() for locating
        # the imgui.ini and other config files.
        list(APPEND SYSTEM_PLATFORM_SOURCES source/configdir_sdl3.cpp)
    endif()
else()
    # SDL3 may already exist as a target from the root CMakeLists.txt:
    #   - Android:           via add_subdirectory(android/SDL3)
    #   - Linux/macOS CI:    via FetchContent_Declare(SDL3 ...) when no
    #                        system SDL3 is installed
    # Only call find_package() when the target isn't already available,
    # otherwise CMake errors out trying to locate an SDL3Config.cmake
    # that doesn't exist (FetchContent doesn't install one).
    if(NOT TARGET SDL3::SDL3)
        find_package(SDL3 REQUIRED)
    endif()
    set(SYSTEM_PLATFORM_SOURCES
        source/date_sdl3.cpp
        source/debug_sdl3.cpp
        source/error_sdl3.cpp
        source/file_sdl3.cpp
        source/fileasync_sdl3.cpp
        source/filesys_sdl3.cpp
        source/registry_sdl3.cpp
        source/text_sdl3.cpp
        source/time_sdl3.cpp
    )
endif()

add_library(system STATIC ${SYSTEM_COMMON_SOURCES} ${SYSTEM_PLATFORM_SOURCES})

target_include_directories(system
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(WIN32)
    # time.cpp uses timeBeginPeriod/timeEndPeriod (winmm)
    # registry.cpp uses SHDeleteKeyA (shlwapi)
    target_link_libraries(system PUBLIC winmm shlwapi)
else()
    target_link_libraries(system PUBLIC SDL3::SDL3)
endif()

# Compat shims for non-MSVC compilers (GCC/Clang/NDK need intrin.h, tchar.h shims)
if(ALTIRRA_COMPAT_DIR)
    target_include_directories(system BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()
