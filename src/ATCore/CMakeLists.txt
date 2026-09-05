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
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    list(APPEND ATCORE_COMMON_SOURCES
        source/checksum_arm64.cpp
        source/fft_neon.cpp
    )
endif()

# checksum_arm64.cpp uses the ARMv8 SHA2 intrinsics (vsha256hq_u32,
# vsha256h2q_u32). Mainline GCC refuses to inline them unless the
# compile unit's target arch includes +crypto. MSVC and Apple clang
# enable the crypto extension by default.
#
# We add +crypto per-file (not project-wide) so non-crypto code can't
# accidentally use crypto intrinsics ungated. Guarded to GNU only:
# Apple clang on darwin-arm64 rejects -march=armv8-a (it uses
# -mcpu=apple-m1 style flags) and accepts the file as-is.
#
# Every supported 64-bit target (Raspberry Pi 4/5/500 → Cortex-A72/A76,
# Apple Silicon, Ampere, Graviton) ships crypto extensions; enabling
# them at compile time is orthogonal to any runtime CPU-feature dispatch
# in the source.
#
# The broader -flax-vector-conversions relaxation needed by fft_neon.cpp
# and the rest of the NEON codebase is applied globally for GNU+aarch64
# in the top-level CMakeLists.txt.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    set_source_files_properties(source/checksum_arm64.cpp PROPERTIES
        COMPILE_FLAGS "-march=armv8-a+crypto")
endif()

# Windows-only timer service using native threadpool timers.
if(WIN32)
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
