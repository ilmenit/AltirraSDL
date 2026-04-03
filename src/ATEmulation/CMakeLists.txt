add_library(ATEmulation STATIC
    source/acia.cpp
    source/acia6850.cpp
    source/crtc.cpp
    source/ctc.cpp
    source/diskutils.cpp
    source/eeprom.cpp
    source/flash.cpp
    source/phone.cpp
    source/riot.cpp
    source/rtcds1305.cpp
    source/rtcmcp7951x.cpp
    source/rtcv3021.cpp
    source/scsi.cpp
    source/stdafx.cpp
    source/via.cpp
)

target_include_directories(ATEmulation
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(ATEmulation BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATEmulation PUBLIC system ATCore)
