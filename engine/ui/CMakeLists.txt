# aegis_ui — Vulkan-free immediate-mode UI kernel (Milestone 9).
# CPU-only: font, widgets, input. Rendered through aegis_render's bridge.
add_library(aegis_ui STATIC
    src/font_atlas.cpp
    src/ui_context.cpp)
add_library(aegis::ui ALIAS aegis_ui)

target_include_directories(aegis_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_ui PUBLIC aegis::core)
aegis_set_warnings(aegis_ui)
