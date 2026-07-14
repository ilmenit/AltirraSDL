include(ExternalProject)
include(FindThreads)

option(ALTIRRA_ENABLE_FFMPEG_RECORDING
    "Enable FFmpeg/libx264-backed MP4 recording in AltirraSDL" ON)

set(ALTIRRA_FFMPEG_ROOT "" CACHE PATH
    "Path to a prebuilt static FFmpeg prefix (include/, lib/) for AltirraSDL recording")

option(ALTIRRA_FETCH_FFMPEG
    "Fetch and build static x264/FFmpeg for AltirraSDL recording" OFF)

if(ALTIRRA_ENABLE_FFMPEG_RECORDING AND (ANDROID OR ALTIRRA_WASM))
    message(STATUS "FFmpeg recording is not supported on Android/WASM; disabling ALTIRRA_ENABLE_FFMPEG_RECORDING")
    set(ALTIRRA_ENABLE_FFMPEG_RECORDING OFF CACHE BOOL "" FORCE)
endif()

if(NOT ALTIRRA_ENABLE_FFMPEG_RECORDING)
    return()
endif()

if(WIN32 AND NOT ALTIRRA_FFMPEG_ROOT AND NOT ALTIRRA_FETCH_FFMPEG)
    message(STATUS
        "ALTIRRA_ENABLE_FFMPEG_RECORDING=ON but no Windows static FFmpeg prefix was provided; "
        "disabling FFmpeg recording for this configure. Set ALTIRRA_FFMPEG_ROOT to enable it.")
    set(ALTIRRA_ENABLE_FFMPEG_RECORDING OFF CACHE BOOL "" FORCE)
    return()
endif()

if(NOT ALTIRRA_FFMPEG_ROOT AND NOT ALTIRRA_FETCH_FFMPEG)
    if(APPLE OR UNIX)
        set(ALTIRRA_FETCH_FFMPEG ON CACHE BOOL "" FORCE)
    endif()
endif()

set(_ALTIRRA_FFMPEG_PREFIX "")
set(_ALTIRRA_X264_LIB "")

if(ALTIRRA_FFMPEG_ROOT)
    set(_ALTIRRA_FFMPEG_PREFIX "${ALTIRRA_FFMPEG_ROOT}")
    set(_ALTIRRA_X264_LIB "${_ALTIRRA_FFMPEG_PREFIX}/lib/libx264.a")
elseif(ALTIRRA_FETCH_FFMPEG)
    if(WIN32)
        message(FATAL_ERROR
            "ALTIRRA_FETCH_FFMPEG is currently supported only on Linux/macOS.\n"
            "For Windows SDL3 builds, set ALTIRRA_FFMPEG_ROOT to a prefix containing\n"
            "static FFmpeg/libx264 headers and libraries.")
    endif()

    find_program(ALTIRRA_MAKE_PROGRAM NAMES gmake make REQUIRED)

    set(_ALTIRRA_FFMPEG_PREFIX "${CMAKE_BINARY_DIR}/_deps/ffmpeg-prefix")
    set(_ALTIRRA_X264_PREFIX "${CMAKE_BINARY_DIR}/_deps/x264-prefix")
    set(_ALTIRRA_X264_LIB "${_ALTIRRA_X264_PREFIX}/lib/libx264.a")
    file(MAKE_DIRECTORY
        "${_ALTIRRA_FFMPEG_PREFIX}/include"
        "${_ALTIRRA_FFMPEG_PREFIX}/lib"
        "${_ALTIRRA_X264_PREFIX}/include"
        "${_ALTIRRA_X264_PREFIX}/lib")

    set(_ALTIRRA_COMMON_ENV
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        AR=${CMAKE_AR}
        RANLIB=${CMAKE_RANLIB}
    )

    set(_ALTIRRA_X264_CONFIGURE
        --prefix=${_ALTIRRA_X264_PREFIX}
        --enable-static
        --disable-cli
        --disable-opencl
        --disable-asm
        --enable-pic
    )

    ExternalProject_Add(altirra_x264
        GIT_REPOSITORY https://code.videolan.org/videolan/x264.git
        GIT_TAG stable
        GIT_SHALLOW TRUE
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND} -E env
            ${_ALTIRRA_COMMON_ENV}
            <SOURCE_DIR>/configure
            ${_ALTIRRA_X264_CONFIGURE}
        BUILD_COMMAND
            ${CMAKE_COMMAND} -E env
            ${_ALTIRRA_COMMON_ENV}
            ${ALTIRRA_MAKE_PROGRAM}
        INSTALL_COMMAND
            ${CMAKE_COMMAND} -E env
            ${_ALTIRRA_COMMON_ENV}
            ${ALTIRRA_MAKE_PROGRAM} install
        BUILD_BYPRODUCTS
            ${_ALTIRRA_X264_PREFIX}/lib/libx264.a
    )

    set(_ALTIRRA_FFMPEG_CONFIGURE
        --prefix=${_ALTIRRA_FFMPEG_PREFIX}
        --pkg-config-flags=--static
        --extra-cflags=-I${_ALTIRRA_X264_PREFIX}/include
        --extra-ldflags=-L${_ALTIRRA_X264_PREFIX}/lib
        --enable-gpl
        --enable-libx264
        --disable-programs
        --disable-doc
        --disable-network
        --disable-avdevice
        --disable-avfilter
        --disable-postproc
        --disable-autodetect
        --disable-debug
        --disable-asm
        --disable-x86asm
        --disable-shared
        --enable-static
        --enable-pic
        --disable-everything
        --enable-protocol=file
        --enable-muxer=mp4
        --enable-muxer=mov
        --enable-encoder=aac
        --enable-encoder=libx264
    )

    ExternalProject_Add(altirra_ffmpeg
        GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
        GIT_TAG n7.1.1
        GIT_SHALLOW TRUE
        UPDATE_COMMAND ""
        DEPENDS altirra_x264
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND} -E env
            ${_ALTIRRA_COMMON_ENV}
            PKG_CONFIG_PATH=${_ALTIRRA_X264_PREFIX}/lib/pkgconfig
            <SOURCE_DIR>/configure
            ${_ALTIRRA_FFMPEG_CONFIGURE}
        BUILD_COMMAND
            ${CMAKE_COMMAND} -E env
            ${_ALTIRRA_COMMON_ENV}
            PKG_CONFIG_PATH=${_ALTIRRA_X264_PREFIX}/lib/pkgconfig
            ${ALTIRRA_MAKE_PROGRAM}
        INSTALL_COMMAND
            ${CMAKE_COMMAND} -E env
            ${_ALTIRRA_COMMON_ENV}
            PKG_CONFIG_PATH=${_ALTIRRA_X264_PREFIX}/lib/pkgconfig
            ${ALTIRRA_MAKE_PROGRAM} install
        BUILD_BYPRODUCTS
            ${_ALTIRRA_FFMPEG_PREFIX}/lib/libavcodec.a
            ${_ALTIRRA_FFMPEG_PREFIX}/lib/libavformat.a
            ${_ALTIRRA_FFMPEG_PREFIX}/lib/libavutil.a
            ${_ALTIRRA_FFMPEG_PREFIX}/lib/libswresample.a
            ${_ALTIRRA_FFMPEG_PREFIX}/lib/libswscale.a
    )
endif()

if(NOT EXISTS "${_ALTIRRA_FFMPEG_PREFIX}/include/libavcodec/avcodec.h" AND NOT TARGET altirra_ffmpeg)
    message(FATAL_ERROR
        "ALTIRRA_ENABLE_FFMPEG_RECORDING is ON, but FFmpeg headers were not found.\n"
        "Set ALTIRRA_FFMPEG_ROOT to a static FFmpeg prefix or enable ALTIRRA_FETCH_FFMPEG.")
endif()

if(NOT EXISTS "${_ALTIRRA_X264_LIB}" AND NOT TARGET altirra_x264)
    message(FATAL_ERROR
        "ALTIRRA_ENABLE_FFMPEG_RECORDING is ON, but libx264.a was not found.\n"
        "Provide a prefix containing lib/libx264.a or enable ALTIRRA_FETCH_FFMPEG.")
endif()

add_library(altirra_ffmpeg_x264 STATIC IMPORTED GLOBAL)
set_target_properties(altirra_ffmpeg_x264 PROPERTIES
    IMPORTED_LOCATION "${_ALTIRRA_X264_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ALTIRRA_FFMPEG_PREFIX}/include")

add_library(altirra_ffmpeg_avutil STATIC IMPORTED GLOBAL)
set_target_properties(altirra_ffmpeg_avutil PROPERTIES
    IMPORTED_LOCATION "${_ALTIRRA_FFMPEG_PREFIX}/lib/libavutil.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_ALTIRRA_FFMPEG_PREFIX}/include")

add_library(altirra_ffmpeg_swresample STATIC IMPORTED GLOBAL)
set_target_properties(altirra_ffmpeg_swresample PROPERTIES
    IMPORTED_LOCATION "${_ALTIRRA_FFMPEG_PREFIX}/lib/libswresample.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_ALTIRRA_FFMPEG_PREFIX}/include")

add_library(altirra_ffmpeg_swscale STATIC IMPORTED GLOBAL)
set_target_properties(altirra_ffmpeg_swscale PROPERTIES
    IMPORTED_LOCATION "${_ALTIRRA_FFMPEG_PREFIX}/lib/libswscale.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_ALTIRRA_FFMPEG_PREFIX}/include")

add_library(altirra_ffmpeg_avcodec STATIC IMPORTED GLOBAL)
set_target_properties(altirra_ffmpeg_avcodec PROPERTIES
    IMPORTED_LOCATION "${_ALTIRRA_FFMPEG_PREFIX}/lib/libavcodec.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_ALTIRRA_FFMPEG_PREFIX}/include")

add_library(altirra_ffmpeg_avformat STATIC IMPORTED GLOBAL)
set_target_properties(altirra_ffmpeg_avformat PROPERTIES
    IMPORTED_LOCATION "${_ALTIRRA_FFMPEG_PREFIX}/lib/libavformat.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_ALTIRRA_FFMPEG_PREFIX}/include")

if(TARGET altirra_ffmpeg)
    add_dependencies(altirra_ffmpeg_x264 altirra_x264)
    add_dependencies(altirra_ffmpeg_avutil altirra_ffmpeg)
    add_dependencies(altirra_ffmpeg_swresample altirra_ffmpeg)
    add_dependencies(altirra_ffmpeg_swscale altirra_ffmpeg)
    add_dependencies(altirra_ffmpeg_avcodec altirra_ffmpeg)
    add_dependencies(altirra_ffmpeg_avformat altirra_ffmpeg)
endif()

target_link_libraries(altirra_ffmpeg_swresample INTERFACE altirra_ffmpeg_avutil)
target_link_libraries(altirra_ffmpeg_swscale INTERFACE altirra_ffmpeg_avutil)
target_link_libraries(altirra_ffmpeg_avcodec INTERFACE altirra_ffmpeg_x264 altirra_ffmpeg_swresample altirra_ffmpeg_swscale altirra_ffmpeg_avutil)
target_link_libraries(altirra_ffmpeg_avformat INTERFACE altirra_ffmpeg_avcodec altirra_ffmpeg_avutil)

add_library(altirra_ffmpeg_bundle INTERFACE)
target_link_libraries(altirra_ffmpeg_bundle INTERFACE
    altirra_ffmpeg_avformat
    altirra_ffmpeg_avcodec
    altirra_ffmpeg_x264
    altirra_ffmpeg_swresample
    altirra_ffmpeg_swscale
    altirra_ffmpeg_avutil
    Threads::Threads)

if(UNIX AND NOT APPLE)
    target_link_libraries(altirra_ffmpeg_bundle INTERFACE m dl)
elseif(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(altirra_ffmpeg_bundle INTERFACE ${COREFOUNDATION_FRAMEWORK})
endif()
