# aegis_offscreen — surface-free offscreen rendering + PNG writer (Milestone 5)

add_library(aegis_offscreen STATIC
    src/offscreen_renderer.cpp
    src/png_writer.cpp)
add_library(aegis::offscreen ALIAS aegis_offscreen)

target_include_directories(aegis_offscreen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_offscreen PUBLIC aegis::core aegis::memory aegis::vk aegis::renderer2d aegis::miniz)
aegis_set_warnings(aegis_offscreen)
