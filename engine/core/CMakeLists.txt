add_library(aegis_core STATIC
    src/assert.cpp
    src/log.cpp
    src/result.cpp)
add_library(aegis::core ALIAS aegis_core)

target_include_directories(aegis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
aegis_set_warnings(aegis_core)
