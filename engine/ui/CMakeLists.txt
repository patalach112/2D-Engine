# aegis_ui — Vulkan-free immediate-mode UI kernel (Milestone 9).
add_library(aegis_ui OBJECT
    src/font_atlas.cpp
    src/ui_context.cpp)
aegis_engine_module(aegis_ui)
