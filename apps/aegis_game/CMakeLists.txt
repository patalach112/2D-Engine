# aegis_game — Milestone 9: VAB → Flight → Map, the full SFS-style loop.
# aegis_game_session is Vulkan-free (linked into aegis_tests for headless
# coverage); aegis_game_scenes turns session state into renderer2d calls and
# is shared with the aegis_game_check verification tool.
add_library(aegis_game_session STATIC game_session.cpp)
add_library(aegis::game_session ALIAS aegis_game_session)
target_include_directories(aegis_game_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aegis_game_session PUBLIC aegis::core aegis::orbit aegis::parts aegis::physics)
aegis_set_warnings(aegis_game_session)

add_library(aegis_game_scenes STATIC scene.cpp)
add_library(aegis::game_scenes ALIAS aegis_game_scenes)
target_include_directories(aegis_game_scenes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aegis_game_scenes PUBLIC
    aegis_game_session aegis::render aegis::ui aegis::renderer2d aegis::orbit aegis::physics aegis::parts)
aegis_set_warnings(aegis_game_scenes)

add_executable(aegis_game main.cpp)

if(AEGIS_WINDOWED)
    target_compile_definitions(aegis_game PRIVATE AEGIS_WINDOWED=1)
    target_link_libraries(aegis_game PRIVATE aegis_game_scenes aegis::renderer2d aegis::orbit aegis::vk aegis::core glfw)
    if(AEGIS_GLFW_NULL_PLATFORM)
        target_compile_definitions(aegis_game PRIVATE AEGIS_GLFW_NULL_PLATFORM=1)
    else()
        target_compile_definitions(aegis_game PRIVATE AEGIS_GLFW_NULL_PLATFORM=0)
    endif()
else()
    target_compile_definitions(aegis_game PRIVATE AEGIS_WINDOWED=0 AEGIS_GLFW_NULL_PLATFORM=0)
    target_link_libraries(aegis_game PRIVATE aegis::core)
endif()

aegis_set_warnings(aegis_game)
# Never registered with ctest — opens a window. Run manually on a
# machine with an X11/Wayland session and a Vulkan 1.3 driver.
