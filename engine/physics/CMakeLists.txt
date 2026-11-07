# Aegis2D — physics module (Milestone 8): rigid-body flight dynamics.
# Vulkan-free; depends on aegis::core, aegis::orbit (Elements oracle) and
# aegis::parts (assemblies, staging, propellant).
add_library(aegis_physics STATIC
    src/model.cpp
    src/vehicle_flight.cpp)
add_library(aegis::physics ALIAS aegis_physics)

target_include_directories(aegis_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_physics PUBLIC aegis::core aegis::orbit aegis::parts)

aegis_set_warnings(aegis_physics)
