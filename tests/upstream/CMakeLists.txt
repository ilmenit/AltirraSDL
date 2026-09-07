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
    ${PROJECT_SOURCE_DIR}/src/compat
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default
)
target_link_libraries(altirra-upstream-regression-test PRIVATE
    ATCore Kasumi system SDL3::SDL3)
add_test(NAME upstream-regressions COMMAND altirra-upstream-regression-test)
add_executable(altirra-vbxe-regression-test
    vbxe.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/vbxe.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/vbxestate.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/gtiatables.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/memorymanager.cpp
    ${PROJECT_SOURCE_DIR}/src/Altirra/source/trace.cpp)
target_include_directories(altirra-vbxe-regression-test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/AltirraSDL/stubs
    ${PROJECT_SOURCE_DIR}/src/Altirra/h
    ${PROJECT_SOURCE_DIR}/src/compat
    ${PROJECT_SOURCE_DIR}/src/Altirra/autobuild_default)
target_link_libraries(altirra-vbxe-regression-test PRIVATE ATCore Kasumi system SDL3::SDL3)
add_test(NAME upstream-vbxe COMMAND altirra-vbxe-regression-test)
set_tests_properties(upstream-regressions upstream-vbxe PROPERTIES TIMEOUT 60)
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
