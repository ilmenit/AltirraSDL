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

# Per-file SIMD flags for GCC/Clang on aarch64 (not needed on MSVC, which
# enables ARMv8 crypto extensions by default and accepts implicit NEON
# vector-type reinterprets; Apple clang behaves the same way).
#
#  * checksum_arm64.cpp uses the ARMv8 SHA2 intrinsics (vsha256hq_u32,
#    vsha256h2q_u32). GCC refuses to inline them unless the compile
#    unit's target arch includes +crypto. Every supported 64-bit target
#    (Raspberry Pi 4/5/500 → Cortex-A72/A76, Apple Silicon, Ampere)
#    ships crypto extensions, and any runtime CPU-feature dispatch in
#    the source is orthogonal to whether the intrinsic is available to
#    the compiler.
#
#  * fft_neon.cpp deliberately reinterprets NEON vector bit patterns
#    across element-count / signedness boundaries (e.g. vmov_n_u64 ->
#    vshll_n_u16). MSVC and Apple clang accept these implicitly;
#    mainline GCC's NEON intrinsic type checking rejects them without
#    -flax-vector-conversions. We scope the flag per-file so stricter
#    checking still applies to every other translation unit.
# Guarded to GNU only: Apple clang on darwin-arm64 rejects -march=armv8-a
# (it uses -mcpu=apple-m1 style flags), and both Apple and mainline clang
# already accept these source files without any additional flags. Only
# mainline GCC needs this treatment.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    set_source_files_properties(source/checksum_arm64.cpp PROPERTIES
        COMPILE_FLAGS "-march=armv8-a+crypto -flax-vector-conversions")
    set_source_files_properties(source/fft_neon.cpp PROPERTIES
        COMPILE_FLAGS "-flax-vector-conversions")
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
