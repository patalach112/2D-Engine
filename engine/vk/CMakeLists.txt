# aegis_vk — Vulkan bootstrap (Milestone 1+)
add_library(aegis_vk OBJECT
    src/common.cpp
    src/instance.cpp
    src/physical_device.cpp
    src/device.cpp
    src/swapchain.cpp)

if(AEGIS_WINDOWED)
    target_sources(aegis_vk PRIVATE src/surface_glfw.cpp)
    target_link_libraries(aegis_vk PRIVATE aegis::glfw)
endif()
target_link_libraries(aegis_vk PRIVATE aegis::volk)

aegis_engine_module(aegis_vk)
