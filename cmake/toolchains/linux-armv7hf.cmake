set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

# The shared VirtualDub system headers use C++23 `if consteval`, which requires
# GCC 12 or newer.  Ubuntu's default `gcc-arm-linux-gnueabihf` metapackage is
# still GCC 11 on 22.04, so prefer a versioned cross compiler when one is
# installed and fall back to the unversioned name otherwise.  An explicit
# -DCMAKE_C_COMPILER / -DCMAKE_CXX_COMPILER on the command line always wins.
if(NOT CMAKE_C_COMPILER)
    find_program(ALTIRRA_ARMV7_CC NAMES
        arm-linux-gnueabihf-gcc-14
        arm-linux-gnueabihf-gcc-13
        arm-linux-gnueabihf-gcc-12
        arm-linux-gnueabihf-gcc)
    set(CMAKE_C_COMPILER "${ALTIRRA_ARMV7_CC}")
endif()

if(NOT CMAKE_CXX_COMPILER)
    find_program(ALTIRRA_ARMV7_CXX NAMES
        arm-linux-gnueabihf-g++-14
        arm-linux-gnueabihf-g++-13
        arm-linux-gnueabihf-g++-12
        arm-linux-gnueabihf-g++)
    set(CMAKE_CXX_COMPILER "${ALTIRRA_ARMV7_CXX}")
endif()

set(ALTIRRA_ARMV7_FLAGS "-marm -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT "${ALTIRRA_ARMV7_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ALTIRRA_ARMV7_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
