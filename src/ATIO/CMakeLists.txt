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
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|ARM64)")
    list(APPEND ATIO_SOURCES source/audioreaderflac_arm64.cpp)
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
