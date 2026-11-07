# aegis_parts — grid-based part system, node snapping, staging, mass
# properties (Milestone 7). Headless; pure data + math.
add_library(aegis_parts OBJECT
    src/part_template.cpp
    src/vehicle_assembly.cpp
    src/staging.cpp)
aegis_engine_module(aegis_parts)
