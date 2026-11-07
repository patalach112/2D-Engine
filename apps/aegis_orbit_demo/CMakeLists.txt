# aegis_orbit_demo — Milestone 4 demo: 2D zoom camera + orbital trajectory
# (orbit path polyline, velocity vector) on the instanced batch renderer.
add_executable(aegis_orbit_demo main.cpp)

if(AEGIS_WINDOWED)
    target_compile_definitions(aegis_orbit_demo PRIVATE AEGIS_WINDOWED=1)
    target_link_libraries(aegis_orbit_demo PRIVATE aegis::renderer2d aegis::orbit aegis::vk aegis::core glfw)
    if(AEGIS_GLFW_NULL_PLATFORM)
        target_compile_definitions(aegis_orbit_demo PRIVATE AEGIS_GLFW_NULL_PLATFORM=1)
    else()
        target_compile_definitions(aegis_orbit_demo PRIVATE AEGIS_GLFW_NULL_PLATFORM=0)
    endif()
else()
    target_compile_definitions(aegis_orbit_demo PRIVATE AEGIS_WINDOWED=0 AEGIS_GLFW_NULL_PLATFORM=0)
    target_link_libraries(aegis_orbit_demo PRIVATE aegis::core)
endif()

aegis_set_warnings(aegis_orbit_demo)
# NOTE: never registered with ctest — opens a window. Run manually on a
# machine with an X11/Wayland session and a Vulkan 1.3 driver.
