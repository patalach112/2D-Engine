# aegis_render — read-only views of the simulation, turned into GPU instances.
# Bridges parts/physics/orbit state into renderer2d calls. Adds no simulation
# state of its own — deterministic for identical inputs.
add_library(aegis_render OBJECT
    src/camera_rig.cpp
    src/part_renderer.cpp
    src/planet_renderer.cpp
    src/ui_bridge.cpp)
aegis_engine_module(aegis_render)
