# Aegis2D — parts module (Milestone 7): grid-based part system, node snapping,
# staging, mass properties. Headless, pure data + math; no Vulkan dependency.

add_library(aegis_parts STATIC
    src/part_template.cpp
    src/vehicle_assembly.cpp
    src/staging.cpp)
add_library(aegis::parts ALIAS aegis_parts)

target_include_directories(aegis_parts
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(aegis_parts PUBLIC aegis::core)

aegis_set_warnings(aegis_parts)
