add_library(ATVM STATIC
    source/compiler.cpp
    source/stdafx.cpp
    source/vm.cpp
)

target_include_directories(ATVM
    PUBLIC ${ALTIRRA_INCLUDE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/h
)

if(ALTIRRA_COMPAT_DIR)
    target_include_directories(ATVM BEFORE PRIVATE ${ALTIRRA_COMPAT_DIR})
endif()

target_link_libraries(ATVM PUBLIC system)
