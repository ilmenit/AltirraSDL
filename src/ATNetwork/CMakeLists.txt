add_library(ATNetwork STATIC
    source/dhcpd.cpp
    source/ethernetbus.cpp
    source/ethernetframe.cpp
    source/gatewayserver.cpp
    source/ipstack.cpp
    source/socket.cpp
    source/stdafx.cpp
    source/tcp.cpp
    source/tcpstack.cpp
    source/udp.cpp
    source/udpstack.cpp
)

target_include_directories(ATNetwork
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(ATNetwork BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATNetwork PUBLIC system ATCore)
