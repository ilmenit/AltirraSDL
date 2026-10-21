# Use the root's non-MSVC shim path. On MSVC, src/compat/intrin.h
# would shadow the native header and hide _ReturnAddress from <future>.
add_executable(altirra-upstream-regression-test
    regression.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/printerfx80.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/printerfontfx80.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/printeroutput.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/encode_png.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/common_png.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/printerexport.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/printerttfencoder.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/printerrasterizer.cpp
)
target_include_directories(altirra-upstream-regression-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default
)
target_link_libraries(altirra-upstream-regression-test PRIVATE
    ATCore Kasumi system SDL3::SDL3)
add_test(NAME upstream-regressions COMMAND altirra-upstream-regression-test)
add_executable(altirra-vbxe-regression-test
    vbxe.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/savestateio.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/vbxe.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/vbxestate.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/gtiatables.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/memorymanager.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/trace.cpp)
target_include_directories(altirra-vbxe-regression-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-vbxe-regression-test PRIVATE ATIO vdjson ATCore Kasumi system SDL3::SDL3)
add_test(NAME upstream-vbxe COMMAND altirra-vbxe-regression-test)
set_tests_properties(upstream-regressions upstream-vbxe PROPERTIES TIMEOUT 60)

# Contract test for the fork's cooperative VDLazyTimer backend
# (src/system/source/time_sdl3.cpp). Win32 uses SetTimer/WM_TIMER instead,
# so there is nothing here to pin down on that platform.
if(NOT WIN32)
    add_executable(altirra-lazy-timer-test lazytimer.cpp)
    target_include_directories(altirra-lazy-timer-test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
        ${PROJECT_SOURCE_DIR}/src/Altirra/h
        ${ALTIRRA_COMPAT_DIR}
        ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default
    )
    target_link_libraries(altirra-lazy-timer-test PRIVATE system SDL3::SDL3)
    add_test(NAME lazy-timer-contract COMMAND altirra-lazy-timer-test)
    set_tests_properties(lazy-timer-contract PROPERTIES TIMEOUT 60)
endif()
set_tests_properties(upstream-regressions PROPERTIES FIXTURES_SETUP upstream_exports)
option(ALTIRRA_REQUIRE_EXPORT_VALIDATION
    "Require Python and Poppler for upstream export regression tests" OFF)
if(ALTIRRA_REQUIRE_EXPORT_VALIDATION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(ALTIRRA_PDFTOPPM pdftoppm REQUIRED)
    find_program(ALTIRRA_PDFTOTEXT pdftotext REQUIRED)
else()
    find_package(Python3 QUIET COMPONENTS Interpreter)
endif()
if(Python3_Interpreter_FOUND)
    set(export_validation_args)
    if(ALTIRRA_REQUIRE_EXPORT_VALIDATION)
        list(APPEND export_validation_args --require-pdf
            --pdftoppm "${ALTIRRA_PDFTOPPM}" --pdftotext "${ALTIRRA_PDFTOTEXT}")
    endif()
    add_test(NAME upstream-export-validation
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/validate_exports.py
            ${export_validation_args})
    set_tests_properties(upstream-export-validation PROPERTIES
        FIXTURES_REQUIRED upstream_exports TIMEOUT 60)
    add_test(NAME upstream-export-requires-tools
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/validate_exports.py
            --require-pdf --pdftoppm "${CMAKE_CURRENT_BINARY_DIR}/missing-pdftoppm")
    set_tests_properties(upstream-export-requires-tools PROPERTIES
        FIXTURES_REQUIRED upstream_exports WILL_FAIL TRUE TIMEOUT 60)
endif()

add_executable(altirra-test22-regression-test
    test22.cpp
    ${PROJECT_SOURCE_DIR}/src/ATTest/source/TestJson.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/diskvirtimage.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/diskvirtimagebase.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/diskvirtimagesdfs.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/os/directorywatcher_sdl3.cpp)
target_compile_definitions(altirra-test22-regression-test PRIVATE AT_TESTS_ENABLED=1)
target_include_directories(altirra-test22-regression-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test22_support
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-test22-regression-test PRIVATE ATIO vdjson ATCore system SDL3::SDL3)
add_test(NAME upstream-test22 COMMAND altirra-test22-regression-test)
set_tests_properties(upstream-test22 PROPERTIES TIMEOUT 60)

add_executable(altirra-test22-cpu-test
    cpu65816.cpp
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/app/cputest_runner.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/cpu.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/cpu6502.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/cpu6502ill.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/cpu65c02.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/cpu65c816.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/savestate.cpp)
target_include_directories(altirra-test22-cpu-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/app
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-test22-cpu-test PRIVATE ATCPU ATCore vdjson system SDL3::SDL3)
add_test(NAME upstream-test22-cpu COMMAND altirra-test22-cpu-test
    ${CMAKE_CURRENT_SOURCE_DIR}/cpu65816-test22.json)
set_tests_properties(upstream-test22-cpu PROPERTIES TIMEOUT 60)

add_executable(altirra-display-regression-test
    display.cpp
    ${PROJECT_SOURCE_DIR}/src/VDDisplay/source/screenfx.cpp
    ${PROJECT_SOURCE_DIR}/src/VDDisplay/source/bloom.cpp)
target_include_directories(altirra-display-regression-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${PROJECT_SOURCE_DIR}/src/VDDisplay/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-display-regression-test PRIVATE system SDL3::SDL3)
add_test(NAME upstream-display COMMAND altirra-display-regression-test)
set_tests_properties(upstream-display PROPERTIES TIMEOUT 60)

add_executable(altirra-video-output-test
    video_output.cpp
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/display/video_output.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/videomanager.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/simeventmanager.cpp)
target_include_directories(altirra-video-output-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/display
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-video-output-test PRIVATE ATCore Kasumi system SDL3::SDL3)
add_test(NAME video-output-routing COMMAND altirra-video-output-test)

add_executable(altirra-instance-reuse-test-fixture
    ../instance_reuse_fixture.cpp
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/app/instance_reuse.cpp)
target_include_directories(altirra-instance-reuse-test-fixture PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/app)
if(WIN32)
    target_link_libraries(altirra-instance-reuse-test-fixture PRIVATE ws2_32 advapi32)
endif()
if(Python3_Interpreter_FOUND)
    add_test(NAME instance-reuse
        COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/test_instance_reuse.py
            $<TARGET_FILE:altirra-instance-reuse-test-fixture>)
    set_tests_properties(instance-reuse PROPERTIES TIMEOUT 120)
endif()

add_executable(altirra-frame-scheduler-test frame_scheduler.cpp)
target_include_directories(altirra-frame-scheduler-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/source/app)
add_test(NAME frame-scheduler COMMAND altirra-frame-scheduler-test)

add_executable(altirra-display-sampling-test display_sampling.cpp)
target_include_directories(altirra-display-sampling-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/h
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-display-sampling-test PRIVATE SDL3::SDL3)
add_test(NAME upstream-display-sampling COMMAND altirra-display-sampling-test)
set_tests_properties(upstream-display-sampling PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 60)

add_executable(altirra-ramcart-regression-test
    ramcart_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/uiregistry.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/cartridge.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/memorymanager.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/savestate.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/savestateio.cpp)
target_include_directories(altirra-ramcart-regression-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${PROJECT_SOURCE_DIR}/src/Altirra/res
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-ramcart-regression-test PRIVATE
    ATIO ATEmulation vdjson ATCore Kasumi system SDL3::SDL3)
add_test(NAME ramcart-regressions COMMAND altirra-ramcart-regression-test)
set_tests_properties(ramcart-regressions PROPERTIES TIMEOUT 60)

add_executable(altirra-expansion-device-test
    expansion_devices.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/mosaic.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/af80.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/memorymanager.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/savestate.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/savestateio.cpp)
target_include_directories(altirra-expansion-device-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${ALTIRRA_COMPAT_DIR}
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-expansion-device-test PRIVATE
    ATIO vdjson ATCore Kasumi system SDL3::SDL3)
add_test(NAME expansion-devices COMMAND altirra-expansion-device-test)
set_tests_properties(expansion-devices PROPERTIES TIMEOUT 60)
