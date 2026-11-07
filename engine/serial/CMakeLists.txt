# Aegis2D — serial module (Milestone 10): strict JSON subset + craft
# serialization. Headless, Vulkan-free; pure data and math.

add_library(aegis_serial STATIC
    src/json.cpp
    src/craft.cpp
    src/craft_store.cpp)
add_library(aegis::serial ALIAS aegis_serial)

target_include_directories(aegis_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(aegis_serial PUBLIC aegis::core aegis::parts)

aegis_set_warnings(aegis_serial)
