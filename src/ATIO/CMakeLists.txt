set(ATIO_SOURCES
    source/atfs.cpp
    source/audioreader.cpp
    source/audioreaderflac.cpp
    source/audioreadervorbis.cpp
    source/audioreaderwav.cpp
    source/audioutils.cpp
    source/blobimage.cpp
    source/cartridgeimage.cpp
    source/cartridgetypes.cpp
    source/cassetteaudiofilters.cpp
    source/cassetteblock.cpp
    source/cassettedecoder.cpp
    source/cassetteimage.cpp
    source/diskfs.cpp
    source/diskfsarc.cpp
    source/diskfscpm.cpp
    source/diskfsdos2.cpp
    source/diskfsdos3.cpp
    source/diskfssdx2.cpp
    source/diskfssdx2util.cpp
    source/diskfsutil.cpp
    source/diskimage.cpp
    source/diskimageutil.cpp
    source/image.cpp
    source/vdiskimageimpl.cpp
    source/partitiondiskview.cpp
    source/partitiontable.cpp
    source/programimage.cpp
    source/savestate.cpp
    source/stdafx.cpp
    source/tapepiecetable.cpp
    source/vorbisbitreader.cpp
    source/vorbisdecoder.cpp
    source/vorbismisc.cpp
    source/wav.cpp
)

# Architecture-specific FLAC decoders
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[36]86)")
    list(APPEND ATIO_SOURCES source/audioreaderflac_x86.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    list(APPEND ATIO_SOURCES source/audioreaderflac_arm64.cpp)
endif()

# audioreaderflac_arm64.cpp's FLAC CRC16 fast path uses vmull_p64 /
# vmull_high_p64 (PMULL polynomial multiply), which mainline GCC
# considers part of the ARMv8 crypto/AES extension and refuses to
# inline without +crypto in the target arch. Same class of issue as
# the SHA2 intrinsics in src/ATCore/checksum_arm64.cpp — see that
# CMakeLists for the matching pattern and full rationale.
#
# (The separate -flax-vector-conversions relaxation for the LPC inner
# loop's int32x4_t/int64x2_t reinterprets is applied globally for
# GNU+aarch64 in the top-level CMakeLists.txt.)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64|arm64)")
    set_source_files_properties(source/audioreaderflac_arm64.cpp PROPERTIES
        COMPILE_FLAGS "-march=armv8-a+crypto")
endif()

add_library(ATIO STATIC ${ATIO_SOURCES})

target_include_directories(ATIO
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/autogen
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(ATIO BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATIO PUBLIC system ATCore Kasumi)
