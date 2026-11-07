cmake_minimum_required(VERSION 3.24)

project(Aegis2D
        VERSION 0.1.0
        DESCRIPTION "Lightweight, ultra-high-performance 2D engine for space flight simulation (C++20 / Vulkan 1.3)"
        LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake >= 4.0 refuses projects that declare cmake_minimum_required < 3.5
# (glfw 3.4 declares 3.4). Clamp the policy floor for bundled third-party code;
# Aegis2D itself requires 3.24.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
endif()

option(AEGIS_WINDOWED     "Build the GLFW windowing backend (auto-detects X11/Wayland, falls back to the Null platform which compiles everywhere)" ON)
option(AEGIS_BUILD_DEMO   "Build the demo applications"                                ON)
option(AEGIS_BUILD_TESTS  "Build the unit test suite"                                  ON)
option(AEGIS_WERROR       "Treat compiler warnings as errors (first-party code)"       OFF)
option(AEGIS_BUILD_TOOLS  "Build the tools/ command-line utilities (rocket PNG dump)"   ON)

# ---------------------------------------------------------------------------
# Fortran kernel auto-detection (Milestone 6)
# AEGIS_ENABLE_FORTRAN=AUTO (default): use the Fortran orbit kernels when a
# compiler is found, otherwise build the numerically identical C++ fallback.
#               =ON : hard requirement (for CI); configure fails if missing.
#               =OFF: always use the C++ fallback kernels.
# ---------------------------------------------------------------------------
set(AEGIS_ENABLE_FORTRAN "AUTO" CACHE STRING "Fortran orbit kernels: AUTO (default), ON (require) or OFF (C++ fallback)")
set_property(CACHE AEGIS_ENABLE_FORTRAN PROPERTY STRINGS AUTO ON OFF)
option(AEGIS_FORTRAN_NATIVE "Optimize Fortran kernels for the build host (-march=native; off for cross-machine determinism)" OFF)

set(AEGIS_FORTRAN_FOUND OFF)
if(NOT AEGIS_ENABLE_FORTRAN STREQUAL "OFF")
    include(CheckLanguage)
    # CheckLanguage caches CMAKE_Fortran_COMPILER (path on success, NOTFOUND on
    # failure) and skips the probe entirely when the variable is already cached.
    # Drop only the NOTFOUND sentinel so a machine that later gains gfortran is
    # re-probed; explicit compiler paths from the user are preserved.
    if(CMAKE_Fortran_COMPILER MATCHES "NOTFOUND")
        unset(CMAKE_Fortran_COMPILER CACHE)
    endif()
    check_language(Fortran)
    if(CMAKE_Fortran_COMPILER)  # path when enabled, NOTFOUND otherwise (per CheckLanguage docs)
        enable_language(Fortran)
        set(AEGIS_FORTRAN_FOUND ON)
    elseif(AEGIS_ENABLE_FORTRAN STREQUAL "ON")
        message(FATAL_ERROR
            "AEGIS_ENABLE_FORTRAN=ON but no Fortran compiler was found. "
            "Install gfortran (MSYS2/MinGW64: pacman -S mingw-w64-x86_64-gcc-fortran) "
            "or use AEGIS_ENABLE_FORTRAN=AUTO/OFF.")
    else()
        message(STATUS "Aegis2D: no Fortran compiler found — the C++ orbit kernels will be used")
    endif()
else()
    message(STATUS "Aegis2D: AEGIS_ENABLE_FORTRAN=OFF — the C++ orbit kernels will be used")
endif()

include(FetchContent)
include(cmake/AegisDeps.cmake)
include(cmake/AegisWarnings.cmake)

# ---------------------------------------------------------------------------
# Subdirectories (dependency order: low-level first)
# ---------------------------------------------------------------------------
if(AEGIS_BUILD_TESTS)
    # Must precede all add_test() calls (apps may register their own checks).
    enable_testing()
endif()

add_subdirectory(engine/core)        # aegis_core       — no Vulkan, no windowing
add_subdirectory(engine/vk)          # aegis_vk         — Vulkan bootstrap (Milestone 1)
add_subdirectory(engine/memory)      # aegis_memory     — allocators + transfer queue (Milestone 2)
add_subdirectory(engine/orbit)       # aegis_orbit      — Keplerian orbital mechanics (Milestone 4)
add_subdirectory(engine/parts)       # aegis_parts      — part system + staging (Milestone 7)
add_subdirectory(engine/physics)     # aegis_physics    — flight dynamics & staging integration (Milestone 8)
add_subdirectory(engine/renderer2d)  # aegis_renderer2d — instanced 2D batch renderer (Milestone 3+4)
add_subdirectory(engine/offscreen)   # aegis_offscreen  — offscreen rendering + PNG writer (Milestone 5)

if(AEGIS_BUILD_DEMO)
    add_subdirectory(apps/aegis_demo)
    add_subdirectory(apps/aegis_roundtrip)
    add_subdirectory(apps/aegis_batch_demo)
    add_subdirectory(apps/aegis_orbit_demo)
endif()

if(AEGIS_BUILD_TOOLS)
    add_subdirectory(tools/aegis_rocket_dump)
    add_subdirectory(tools/aegis_assembly_check)
    add_subdirectory(tools/aegis_flight_check)
endif()

if(AEGIS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "------------------------------------------------------------------")
message(STATUS "Aegis2D ${PROJECT_VERSION} configured")
message(STATUS "  C++ standard : C++20")
message(STATUS "  Vulkan       : 1.3 headers (v1.3.295) via volk")
message(STATUS "  Windowed     : ${AEGIS_WINDOWED}")
message(STATUS "  Fortran      : ${AEGIS_ENABLE_FORTRAN} (found: ${AEGIS_FORTRAN_FOUND})")
if(AEGIS_WINDOWED)
    message(STATUS "  GLFW         : X11=${GLFW_BUILD_X11} Wayland=${GLFW_BUILD_WAYLAND} NullPlatform=${AEGIS_GLFW_NULL_PLATFORM}")
endif()
message(STATUS "  Tests        : ${AEGIS_BUILD_TESTS}")
message(STATUS "------------------------------------------------------------------")
