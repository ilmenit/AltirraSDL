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
