# Core libraries in dependency order
add_subdirectory(system)
add_subdirectory(vdjson)
add_subdirectory(Kasumi)
add_subdirectory(ATCore)
add_subdirectory(ATCPU)
add_subdirectory(ATEmulation)
add_subdirectory(ATDevices)
add_subdirectory(ATIO)
add_subdirectory(ATAudio)
add_subdirectory(ATNetwork)
add_subdirectory(ATNetworkSockets)
add_subdirectory(ATDebugger)
add_subdirectory(ATVM)

# SDL3+ImGui frontend (Linux, macOS, Android, or Windows with -DALTIRRA_SDL3=ON)
if(ALTIRRA_SDL3)
    add_subdirectory(AltirraSDL)
endif()

# Headless lean SDK target — same bridge protocol as `AltirraSDL --bridge`
# but without ImGui, librashader, the SDL3 frontend's display/input/UI tree,
# embedded fonts, etc. SDL3 is still linked transitively (used only for the
# audio mixer with the dummy driver). See AltirraBridgeServer/main_bridge.cpp
# for the rationale.
#
# Default OFF — opt in with -DALTIRRA_BRIDGE_SERVER=ON. Skipped on Android
# because the lean target is meant for desktop / CI / container use.
option(ALTIRRA_BRIDGE_SERVER "Build the lean headless AltirraBridgeServer" OFF)
if(ALTIRRA_WASM AND ALTIRRA_BRIDGE_SERVER)
    message(WARNING "ALTIRRA_BRIDGE_SERVER is not supported for ALTIRRA_WASM; forcing OFF.")
    set(ALTIRRA_BRIDGE_SERVER OFF CACHE BOOL "Build the lean headless AltirraBridgeServer" FORCE)
endif()
if(ALTIRRA_BRIDGE_SERVER AND ALTIRRA_SDL3 AND NOT ANDROID)
    add_subdirectory(AltirraBridgeServer)
endif()
