add_executable(aegis_roundtrip main.cpp)

target_link_libraries(aegis_roundtrip PRIVATE aegis::memory aegis::vk aegis::core)
aegis_set_warnings(aegis_roundtrip)

if(AEGIS_BUILD_TESTS)
    # Registers as a ctest case. Without a Vulkan driver the binary prints a
    # skip message and exits 0; on machines with a driver it performs the
    # full upload→readback→byte-compare verification.
    add_test(NAME aegis_roundtrip COMMAND aegis_roundtrip)
endif()
