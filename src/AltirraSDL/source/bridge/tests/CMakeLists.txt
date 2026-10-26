if(NOT ALTIRRA_BUILD_TESTS)
	return()
endif()

add_executable(altirra-bridge-pokey-audio-test
	bridge_pokey_audio_test.cpp)

target_include_directories(altirra-bridge-pokey-audio-test PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(altirra-bridge-pokey-audio-test PRIVATE cxx_std_17)

add_test(NAME bridge-pokey-audio
	COMMAND altirra-bridge-pokey-audio-test)

if(TARGET AltirraBridgeServer)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	add_executable(altirra-bridge-vbxe-sdk-test
		bridge_vbxe_sdk_test.c
		../../../AltirraBridge/sdk/c/altirra_bridge.c)
	target_include_directories(altirra-bridge-vbxe-sdk-test PRIVATE
		../../../AltirraBridge/sdk/c)
	if(WIN32)
		target_link_libraries(altirra-bridge-vbxe-sdk-test PRIVATE ws2_32)
	endif()
	add_test(NAME bridge-vbxe
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/bridge_vbxe_test.py
			$<TARGET_FILE:AltirraBridgeServer>
			${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python
			$<TARGET_FILE:altirra-bridge-vbxe-sdk-test>)
	set_tests_properties(bridge-vbxe PROPERTIES
		TIMEOUT 30
		ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
	add_test(NAME bridge-timing
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/bridge_timing_test.py
			$<TARGET_FILE:AltirraBridgeServer>
			${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python)
	set_tests_properties(bridge-timing PROPERTIES
		TIMEOUT 60
		ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
	add_test(NAME bridge-defaults
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/bridge_defaults_test.py
			$<TARGET_FILE:AltirraBridgeServer>
			${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python)
	set_tests_properties(bridge-defaults PROPERTIES
		TIMEOUT 30
		ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
	add_test(NAME bridge-custom-device
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/bridge_custom_device_test.py
			$<TARGET_FILE:AltirraBridgeServer>
			${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python)
	add_test(NAME bridge-profile-address
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/bridge_profile_address_test.py
			$<TARGET_FILE:AltirraBridgeServer>
			${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python)
	set_tests_properties(bridge-profile-address PROPERTIES
		TIMEOUT 30
		ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
	set_tests_properties(bridge-custom-device PROPERTIES
		TIMEOUT 30
		ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
	add_test(NAME bridge-pokey-chip
		COMMAND ${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/bridge_pokey_chip_test.py
			$<TARGET_FILE:AltirraBridgeServer>
			${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python)
	set_tests_properties(bridge-pokey-chip PROPERTIES
		TIMEOUT 30
		ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

if(TARGET AltirraSDL AND ALTIRRA_BRIDGE)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)

	# Exercise the built-in device with no custom-device descriptor.
	# SDL's dummy video + software renderer keep this test display-independent.
	add_test(NAME bridge-antonia2
	    COMMAND ${Python3_EXECUTABLE}
	        ${CMAKE_CURRENT_SOURCE_DIR}/bridge_antonia2_test.py
	        $<TARGET_FILE:AltirraSDL>
	        ${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python
	        --builtin)
	set_tests_properties(bridge-antonia2 PROPERTIES
	    TIMEOUT 60
	    SKIP_RETURN_CODE 77
	    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;SDL_VIDEODRIVER=dummy;ALTIRRA_DISPLAY_BACKEND=sdlrenderer")
endif()

if(TARGET AltirraBridgeServer AND ALTIRRA_MCP)
    add_test(NAME bridge-macro
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tests -p test_bridge_macro.py)
    set_tests_properties(bridge-macro PROPERTIES
        TIMEOUT 30 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    foreach(mcp_test IN ITEMS mcp input)
        add_test(NAME bridge-${mcp_test}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/bridge_${mcp_test}_test.py
                $<TARGET_FILE:AltirraBridgeServer>
                ${CMAKE_SOURCE_DIR}/src/AltirraSDL/AltirraBridge/sdk/python)
        set_tests_properties(bridge-${mcp_test} PROPERTIES
            TIMEOUT 60 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    endforeach()
endif()
