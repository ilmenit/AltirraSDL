set(KASUMI_COMMON_SOURCES
    source/alphablt.cpp
    source/blitter.cpp
    source/blt.cpp
    source/blt_reference.cpp
    source/blt_reference_pal.cpp
    source/blt_reference_rgb.cpp
    source/blt_reference_yuv2yuv.cpp
    source/blt_reference_yuv.cpp
    source/blt_reference_yuvrev.cpp
    source/blt_setup.cpp
    source/blt_spanutils.cpp
    source/blt_uberblit.cpp
    source/pixel.cpp
    source/pixmaputils.cpp
    source/region.cpp
    source/resample.cpp
    source/resample_kernels.cpp
    source/resample_stages.cpp
    source/resample_stages_reference.cpp
    source/stdafx.cpp
    source/stretchblt_reference.cpp
    source/uberblit_16f.cpp
    source/uberblit.cpp
    source/uberblit_gen.cpp
    source/uberblit_resample.cpp
    source/uberblit_resample_special.cpp
    source/uberblit_swizzle.cpp
    source/uberblit_ycbcr_generic.cpp
)

# Architecture-specific SIMD sources
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[36]86)")
    list(APPEND KASUMI_COMMON_SOURCES
        source/region_sse2.cpp
        source/resample_stages_x64.cpp
        source/uberblit_ycbcr_sse2_intrin.cpp
    )
    # The VS .sln build also compiles blt_x86.cpp, resample_stages_x86.cpp,
    # etc. which are thin wrappers calling hand-tuned MASM .asm routines.
    # The CMake build does NOT compile those — it relies on the reference
    # C++ implementations (always compiled above) and the intrinsics files
    # (resample_stages_x64.cpp, uberblit_ycbcr_sse2_intrin.cpp) instead.
    # This avoids pulling in x86 MASM assembly which only the VS toolchain
    # can assemble, and keeps the CMake build portable across all platforms.
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64)")
    list(APPEND KASUMI_COMMON_SOURCES
        source/blt_spanutils_arm64.cpp
        source/region_neon.cpp
        source/resample_stages_arm64.cpp
    )
endif()

add_library(Kasumi STATIC ${KASUMI_COMMON_SOURCES})

# Per-file SIMD flags for GCC/Clang (not needed on MSVC)
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
    set_source_files_properties(source/resample_stages_x64.cpp PROPERTIES COMPILE_FLAGS "-mssse3 -msse4.1")
    set_source_files_properties(source/uberblit_ycbcr_sse2_intrin.cpp PROPERTIES COMPILE_FLAGS "-mssse3")
endif()

target_include_directories(Kasumi
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(Kasumi BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(Kasumi PUBLIC system)
