# aegis_game_check — Milestone 9 tool: renders the built-in game scenes
# (VAB, flight, orbital map) to offscreen RGBA8 images and writes PNGs.
add_executable(aegis_game_check main.cpp)
target_link_libraries(aegis_game_check PRIVATE
    aegis::core aegis::vk aegis::renderer2d aegis::offscreen aegis::game_session aegis::game_scenes)
aegis_set_warnings(aegis_game_check)

# Skips cleanly (exit 0) when no Vulkan loader/driver exists — CI-safe.
if(AEGIS_BUILD_TESTS)
    add_test(NAME aegis_game_check COMMAND aegis_game_check)
endif()
