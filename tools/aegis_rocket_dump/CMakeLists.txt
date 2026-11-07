# aegis_rocket_dump — Milestone 5 tool: renders the deterministic rocket
# scene to an offscreen RGBA8 image and writes a PNG (no window, no swapchain).
add_executable(aegis_rocket_dump main.cpp)
target_link_libraries(aegis_rocket_dump PRIVATE
    aegis::core aegis::vk aegis::renderer2d aegis::orbit aegis::offscreen)
aegis_set_warnings(aegis_rocket_dump)

# Skips cleanly (exit 0) when no Vulkan loader/driver exists — CI-safe.
if(AEGIS_BUILD_TESTS)
    add_test(NAME aegis_rocket_dump COMMAND aegis_rocket_dump)
endif()
