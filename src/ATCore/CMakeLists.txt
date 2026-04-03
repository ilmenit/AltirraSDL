set(ATCORE_COMMON_SOURCES
    source/address.cpp
    source/asyncdispatcherimpl.cpp
    source/atascii.cpp
    source/bussignal.cpp
    source/checksum.cpp
    source/checksum_intrin.cpp
    source/configvar.cpp
    source/consoleoutput.cpp
    source/crc.cpp
    source/decmath.cpp
    source/deferredevent.cpp
    source/deviceimpl.cpp
    source/deviceparentimpl.cpp
    source/devicesioimpl.cpp
    source/devicestorageimpl.cpp
    source/enumparse.cpp
    source/fft.cpp
    source/fft_scalar.cpp
    source/logging.cpp
    source/md5.cpp
    source/memoryutils.cpp
    source/notifylist.cpp
    source/profile.cpp
    source/progress.cpp
    source/propertyset.cpp
    source/randomization.cpp
    source/savestate.cpp
    source/scheduler.cpp
    source/serialization.cpp
    source/sioutils.cpp
    source/snapshotimpl.cpp
    source/stdafx.cpp
    source/vfs.cpp
)

# Architecture-specific
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[36]86)")
    list(APPEND ATCORE_COMMON_SOURCES
        source/fft_sse2.cpp
        source/fft_avx2.cpp
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64)")
    list(APPEND ATCORE_COMMON_SOURCES
        source/checksum_arm64.cpp
        source/fft_neon.cpp
    )
endif()

# Windows-only timer service — excluded when ALTIRRA_SDL3 is ON because
# win32_stubs.cpp provides ATCreateTimerService() as a stub.
if(WIN32 AND NOT ALTIRRA_SDL3)
    list(APPEND ATCORE_COMMON_SOURCES
        source/timerserviceimpl_win32.cpp
    )
endif()

add_library(ATCore STATIC ${ATCORE_COMMON_SOURCES})

target_include_directories(ATCore
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(ATCore BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATCore PUBLIC system)
