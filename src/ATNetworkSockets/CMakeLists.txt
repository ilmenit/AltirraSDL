if(WIN32)
    set(ATNETWORKSOCKETS_SOURCES
        source/nativesockets.cpp
        source/lookupworker.cpp
        source/socketworker.cpp
        source/socketutils_win32.cpp
        source/worker.cpp
        source/vxlantunnel.cpp
        source/stdafx.cpp
    )
else()
    set(ATNETWORKSOCKETS_SOURCES
        source/nativesockets_sdl3.cpp
        source/lookupworker_sdl3.cpp
        source/socketworker_sdl3.cpp
        source/socketutils_posix.cpp
        source/worker_sdl3.cpp
        source/vxlantunnel_sdl3.cpp
        source/stdafx.cpp
    )
endif()

add_library(ATNetworkSockets STATIC ${ATNETWORKSOCKETS_SOURCES})

target_include_directories(ATNetworkSockets
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(NOT WIN32)
    target_include_directories(ATNetworkSockets BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATNetworkSockets PUBLIC system ATCore ATNetwork)
