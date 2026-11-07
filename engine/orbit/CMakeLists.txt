# aegis_orbit — Keplerian orbital mechanics (Vulkan-free, Milestone 4)

add_library(aegis_orbit STATIC
    src/orbit.cpp
    src/orbit_fallback.cpp
    src/ballistic.cpp)
add_library(aegis::orbit ALIAS aegis_orbit)

target_include_directories(aegis_orbit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aegis_orbit PUBLIC aegis::core)

# Fortran kernels (Milestone 6): enabled by the top-level CheckLanguage probe
# (AEGIS_FORTRAN_FOUND). .mod files stay inside the build tree; strict IEEE
# fp semantics by default (no -ffast-math, no fp-contract).
if(AEGIS_FORTRAN_FOUND)
    set(CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fortran_modules)
    target_sources(aegis_orbit PRIVATE fortran/aegis_kernels.f90)
    target_compile_definitions(aegis_orbit PUBLIC AEGIS_HAVE_FORTRAN=1)
    if(CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
        target_compile_options(aegis_orbit PRIVATE -O3 -ffp-contract=off -fimplicit-none)
        if(AEGIS_FORTRAN_NATIVE)
            target_compile_options(aegis_orbit PRIVATE -march=native)
        endif()
    endif()
    message(STATUS "Aegis2D: aegis_orbit — Fortran kernels enabled (${CMAKE_Fortran_COMPILER_ID})")
else()
    message(STATUS "Aegis2D: aegis_orbit — C++ fallback kernels")
endif()

aegis_set_warnings(aegis_orbit)
