# aegis_offscreen — surface-free offscreen rendering + PNG writer (Milestone 5)
add_library(aegis_offscreen OBJECT
    src/offscreen_renderer.cpp
    src/png_writer.cpp)
target_link_libraries(aegis_offscreen PRIVATE aegis::miniz)  # headers stay private to the module
aegis_engine_module(aegis_offscreen)
