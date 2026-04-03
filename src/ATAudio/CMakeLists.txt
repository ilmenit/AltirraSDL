set(ATAUDIO_COMMON_SOURCES
    source/audioconvolutionplayer.cpp
    source/audiofilters.cpp
    source/audiosamplebuffer.cpp
    source/audiosampleplayer.cpp
    source/audiosamplepool.cpp
    source/pokey.cpp
    source/pokeyrenderer.cpp
    source/pokeysavestate.cpp
    source/pokeytables.cpp
    source/stdafx.cpp
)

# Win32 audio output backends — only for native Win32 builds.
# The SDL3 frontend provides its own audiooutput_sdl3.cpp which defines the
# same factory function (ATCreateAudioOutput), so these must be excluded when
# ALTIRRA_SDL3 is ON to avoid duplicate symbol errors.
if(WIN32 AND NOT ALTIRRA_SDL3)
    list(APPEND ATAUDIO_COMMON_SOURCES
        source/audiooutput.cpp
        source/audiooutwasapi.cpp
        source/audiooutwaveout.cpp
        source/audiooutxa2.cpp
    )
endif()

add_library(ATAudio STATIC ${ATAUDIO_COMMON_SOURCES})

target_include_directories(ATAudio
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

target_link_libraries(ATAudio PUBLIC system ATCore)
