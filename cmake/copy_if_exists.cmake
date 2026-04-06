# copy_if_exists.cmake — Copy librashader files matching a glob.
# Used by the package_altirra target to optionally bundle librashader.
# Copies every file matching SRC_GLOB into DST.  If nothing matches,
# prints a friendly skip message instead of failing.
#
# On Linux librashader's loader (vendor/librashader/librashader_ld.h)
# searches for librashader.so, librashader.so.1, and librashader.so.2,
# so we need to ship whichever variant build.sh --librashader produced —
# not just a single hard-coded name.
#
# Usage: cmake -DSRC_GLOB=<pattern> -DDST=<dir> -P copy_if_exists.cmake
file(GLOB _matches "${SRC_GLOB}")
if(_matches)
    foreach(_f IN LISTS _matches)
        get_filename_component(_name "${_f}" NAME)
        file(COPY "${_f}" DESTINATION "${DST}")
        message(STATUS "Bundled ${_name} into package")
    endforeach()
else()
    message(STATUS "No librashader library matched ${SRC_GLOB} — skipping (shader presets will not be available)")
endif()
