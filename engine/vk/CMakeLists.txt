# aegis_vk — Vulkan bootstrap (Milestone 1+)
add_library(aegis_vk STATIC
    src/common.cpp
    src/instance.cpp
    src/physical_device.cpp
    src/device.cpp
    src/swapchain.cpp)
add_library(aegis::vk ALIAS aegis_vk)

if(AEGIS_WINDOWED)
    target_sources(aegis_vk PRIVATE src/surface_glfw.cpp)
    target_link_libraries(aegis_vk PUBLIC aegis::glfw)
endif()

target_include_directories(aegis_vk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_vk PUBLIC aegis::core aegis::volk)
aegis_set_warnings(aegis_vk)
