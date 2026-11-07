# aegis_physics — rigid-body flight dynamics (Milestone 8).
add_library(aegis_physics OBJECT
    src/model.cpp
    src/vehicle_flight.cpp)
aegis_engine_module(aegis_physics)
