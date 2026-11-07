# aegis_orbit — Keplerian orbital mechanics (Vulkan-free, Milestone 4)

add_library(aegis_orbit STATIC
    src/orbit.cpp)
add_library(aegis::orbit ALIAS aegis_orbit)

target_include_directories(aegis_orbit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_orbit PUBLIC aegis::core)
aegis_set_warnings(aegis_orbit)
