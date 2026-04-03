add_library(ATDevices STATIC
    source/blockdevwritefilter.cpp
    source/computereyes.cpp
    source/corvus.cpp
    source/devices.cpp
    source/dongle.cpp
    source/exeloader.cpp
    source/loopback.cpp
    source/modemsound.cpp
    source/netserial.cpp
    source/paralleltoserial.cpp
    source/parfilewriter.cpp
    source/serialsplitter.cpp
    source/sioclock.cpp
    source/stdafx.cpp
    source/supersalt.cpp
    source/testdevicesiohs.cpp
    source/testdevicesiopoll.cpp
    source/videogenerator.cpp
)

target_include_directories(ATDevices
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/autogen
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(ATDevices BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATDevices PUBLIC system ATCore ATCPU)
