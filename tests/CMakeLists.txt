add_executable(aegis_tests
    test_camera.cpp
    test_free_list.cpp
    test_log.cpp
    test_main.cpp
    test_orbit.cpp
    test_projection.cpp
    test_result.cpp
    test_types_version.cpp
    test_vk_common.cpp)

target_include_directories(aegis_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aegis_tests PRIVATE aegis::core aegis::memory aegis::vk aegis::renderer2d aegis::orbit)
aegis_set_warnings(aegis_tests)

add_test(NAME aegis_tests COMMAND aegis_tests)
