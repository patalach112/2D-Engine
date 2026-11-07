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
add_subdirectory(engine/vk)          # aegis_vk         — Vulkan bootstrap

if(AEGIS_BUILD_DEMO)
    add_subdirectory(apps/aegis_demo)
endif()

if(AEGIS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

message(STATUS "------------------------------------------------------------------")
message(STATUS "Aegis2D ${PROJECT_VERSION} configured")
message(STATUS "  C++ standard : C++20")
message(STATUS "  Vulkan       : 1.3 headers (v1.3.295) via volk")
message(STATUS "  Windowed     : ${AEGIS_WINDOWED}")
if(AEGIS_WINDOWED)
    message(STATUS "  GLFW         : X11=${GLFW_BUILD_X11} Wayland=${GLFW_BUILD_WAYLAND} NullPlatform=${AEGIS_GLFW_NULL_PLATFORM}")
endif()
message(STATUS "  Tests        : ${AEGIS_BUILD_TESTS}")
message(STATUS "------------------------------------------------------------------")
