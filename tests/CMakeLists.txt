add_executable(aegis_tests
    test_camera.cpp
    test_free_list.cpp
    test_game_session.cpp
    test_log.cpp
    test_main.cpp
    test_orbit.cpp
    test_orbit_kernels.cpp
    test_parts.cpp
    test_physics.cpp
    test_png_writer.cpp
    test_projection.cpp
    test_result.cpp
    test_serial.cpp
    test_types_version.cpp
    test_ui.cpp
    test_vk_common.cpp)

target_include_directories(aegis_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aegis_tests PRIVATE aegis::core aegis::memory aegis::vk aegis::renderer2d aegis::orbit aegis::offscreen aegis::parts aegis::physics aegis::ui aegis::serial aegis::game_session)
aegis_set_warnings(aegis_tests)

add_test(NAME aegis_tests COMMAND aegis_tests)
