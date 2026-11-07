# aegis_renderer2d — instanced 2D batch renderer (Milestone 3)
# Shaders are compiled at build time by glslang (target glslang-standalone,
# which is the glslangValidator executable; CMake target name is historical).

set(SPV_DIR "${CMAKE_CURRENT_BINARY_DIR}/spv")
set(QUAD_VERT_SPV "${SPV_DIR}/quad.vert.spv")
set(QUAD_FRAG_SPV "${SPV_DIR}/quad.frag.spv")
set(LINE_VERT_SPV "${SPV_DIR}/line.vert.spv")
set(LINE_FRAG_SPV "${SPV_DIR}/line.frag.spv")

add_custom_command(
    OUTPUT "${QUAD_VERT_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPV_DIR}"
    COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.3
            "${PROJECT_SOURCE_DIR}/shaders/quad.vert" -o "${QUAD_VERT_SPV}"
    DEPENDS "${PROJECT_SOURCE_DIR}/shaders/quad.vert" glslang-standalone
    COMMENT "GLSL -> SPIR-V: shaders/quad.vert"
    VERBATIM)

add_custom_command(
    OUTPUT "${QUAD_FRAG_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPV_DIR}"
    COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.3
            "${PROJECT_SOURCE_DIR}/shaders/quad.frag" -o "${QUAD_FRAG_SPV}"
    DEPENDS "${PROJECT_SOURCE_DIR}/shaders/quad.frag" glslang-standalone
    COMMENT "GLSL -> SPIR-V: shaders/quad.frag"
    VERBATIM)

add_custom_command(
    OUTPUT "${LINE_VERT_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPV_DIR}"
    COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.3
            "${PROJECT_SOURCE_DIR}/shaders/line.vert" -o "${LINE_VERT_SPV}"
    DEPENDS "${PROJECT_SOURCE_DIR}/shaders/line.vert" glslang-standalone
    COMMENT "GLSL -> SPIR-V: shaders/line.vert"
    VERBATIM)

add_custom_command(
    OUTPUT "${LINE_FRAG_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPV_DIR}"
    COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.3
            "${PROJECT_SOURCE_DIR}/shaders/line.frag" -o "${LINE_FRAG_SPV}"
    DEPENDS "${PROJECT_SOURCE_DIR}/shaders/line.frag" glslang-standalone
    COMMENT "GLSL -> SPIR-V: shaders/line.frag"
    VERBATIM)

add_custom_target(aegis_renderer2d_shaders ALL DEPENDS "${QUAD_VERT_SPV}" "${QUAD_FRAG_SPV}" "${LINE_VERT_SPV}" "${LINE_FRAG_SPV}")

add_library(aegis_renderer2d STATIC
    src/projection.cpp
    src/camera.cpp
    src/batch_renderer.cpp)
add_library(aegis::renderer2d ALIAS aegis_renderer2d)
add_dependencies(aegis_renderer2d aegis_renderer2d_shaders)

target_include_directories(aegis_renderer2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(aegis_renderer2d PRIVATE AEGIS_SPV_DIR="${SPV_DIR}")
target_link_libraries(aegis_renderer2d PUBLIC aegis::vk aegis::memory)
aegis_set_warnings(aegis_renderer2d)
