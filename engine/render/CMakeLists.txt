# aegis_render — read-only views of the simulation, turned into GPU instances.
# Bridges aegis_parts/aegis_physics/aegis_orbit state into renderer2d calls
# (procedural parts, planet/atmosphere, HUD draw list). Adds no simulation
# state of its own — deterministic for identical inputs.
add_library(aegis_render STATIC
    src/camera_rig.cpp
    src/part_renderer.cpp
    src/planet_renderer.cpp
    src/ui_bridge.cpp)
add_library(aegis::render ALIAS aegis_render)

target_include_directories(aegis_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_render PUBLIC
    aegis::core
    aegis::renderer2d
    aegis::ui
    aegis::parts
    aegis::physics
    aegis::orbit)
aegis_set_warnings(aegis_render)
