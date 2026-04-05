# copy_if_exists.cmake — Copy a file only if it exists.
# Used by the package_altirra target to optionally bundle librashader.
# Usage: cmake -DSRC=<path> -DDST=<dir> -P copy_if_exists.cmake
if(EXISTS "${SRC}")
    get_filename_component(_name "${SRC}" NAME)
    file(COPY "${SRC}" DESTINATION "${DST}")
    message(STATUS "Bundled ${_name} into package")
else()
    get_filename_component(_name "${SRC}" NAME)
    message(STATUS "${_name} not found — skipping (shader presets will not be available)")
endif()
