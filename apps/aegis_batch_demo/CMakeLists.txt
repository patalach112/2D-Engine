add_executable(aegis_batch_demo main.cpp)

if(AEGIS_WINDOWED)
    target_compile_definitions(aegis_batch_demo PRIVATE AEGIS_WINDOWED=1)
    target_link_libraries(aegis_batch_demo PRIVATE aegis::renderer2d aegis::vk aegis::core aegis::glfw)
    if(AEGIS_GLFW_NULL_PLATFORM)
        target_compile_definitions(aegis_batch_demo PRIVATE AEGIS_GLFW_NULL_PLATFORM=1)
    else()
        target_compile_definitions(aegis_batch_demo PRIVATE AEGIS_GLFW_NULL_PLATFORM=0)
    endif()
else()
    target_compile_definitions(aegis_batch_demo PRIVATE AEGIS_WINDOWED=0 AEGIS_GLFW_NULL_PLATFORM=0)
    target_link_libraries(aegis_batch_demo PRIVATE aegis::core)
endif()

aegis_set_warnings(aegis_batch_demo)
# NOTE: never registered with ctest — opens a window. Run manually on a
# machine with an X11/Wayland session and a Vulkan 1.3 driver.
