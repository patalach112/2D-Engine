cmake_minimum_required(VERSION 3.24)

project(Aegis2D
        VERSION 0.2.0
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
include(cmake/AegisEngine.cmake)   # PIC, AEGIS_ENGINE_INCLUDE_DIRS, aegis_engine_module() — before deps so 3rd-party picks up PIC
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
add_subdirectory(engine/serial)     # aegis_serial     — JSON subset + craft save/load (Milestone 10)
add_subdirectory(engine/physics)     # aegis_physics    — flight dynamics & staging integration (Milestone 8)
add_subdirectory(engine/ui)          # aegis_ui         — immediate-mode UI kernel + bitmap font (Milestone 9)
add_subdirectory(engine/render)       # aegis_render     — parts/planet/UI bridges to renderer2d (Milestone 9)
add_subdirectory(engine/renderer2d)  # aegis_renderer2d — instanced 2D batch renderer (Milestone 3+4)
add_subdirectory(engine/offscreen)   # aegis_offscreen  — offscreen rendering + PNG writer (Milestone 5)

# --- aegis_engine: every engine module in ONE shared library -----------------
# .so on Linux / .dll on Windows. All public boundary symbols are annotated
# with AEGIS_API (see engine/core/include/aegis/export.h); unannotated
# symbols stay hidden on Linux and are simply not exported anywhere else.
add_library(aegis_engine SHARED)
target_link_libraries(aegis_engine
    PRIVATE aegis_core aegis_memory aegis_vk aegis_orbit aegis_parts aegis_serial
            aegis_physics aegis_ui aegis_render aegis_renderer2d aegis_offscreen
    PUBLIC  aegis::volk)   # volk.h appears in public headers (buffered Vulkan calls)
if(AEGIS_WINDOWED)
    target_link_libraries(aegis_engine PRIVATE aegis::glfw)
endif()
target_link_libraries(aegis_engine PRIVATE aegis::miniz)   # PNG deflate — headers stay private
# Unix: keep the static 3rd-party archives (volk/glfw/miniz) out of the
# dynamic symbol table entirely — only AEGIS_API symbols cross the boundary.
if(UNIX AND NOT APPLE)
    target_link_options(aegis_engine PRIVATE -Wl,--exclude-libs,ALL)
endif()
target_include_directories(aegis_engine PUBLIC ${AEGIS_ENGINE_INCLUDE_DIRS})
add_library(aegis::engine ALIAS aegis_engine)
# Compatibility aliases — existing module link names now resolve to the one
# shared library, so consumers needed zero changes.
foreach(_module core memory vk orbit parts serial physics ui render renderer2d offscreen)
    add_library(aegis::${_module} ALIAS aegis_engine)
endforeach()

# aegis_game (session + scenes) is always built: the tools and test
# suite link it for headless verification, not just the windowed app.
add_subdirectory(apps/aegis_game)

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
    add_subdirectory(tools/aegis_game_check)
    add_subdirectory(tools/aegis_craft_check)
endif()

if(AEGIS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# --- Post-build: drop the shared engine next to every executable -------------
# A fresh pull that rebuilds aegis_engine leaves stale binaries unusable, so
# sync the library into each consumer's output directory on every build.
set(_aegis_engine_consumers
    aegis_game aegis_demo aegis_roundtrip aegis_batch_demo aegis_orbit_demo
    aegis_rocket_dump aegis_assembly_check aegis_flight_check aegis_game_check aegis_craft_check
    aegis_tests)
foreach(_consumer IN LISTS _aegis_engine_consumers)
    if(TARGET ${_consumer})
        add_custom_command(TARGET aegis_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aegis_engine> $<TARGET_FILE_DIR:${_consumer}>
            COMMENT "aegis_engine -> ${_consumer} output dir")
    endif()
endforeach()

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
# --- Post-build: drop the shared engine next to every executable -------------
# A fresh pull that rebuilds aegis_engine leaves stale binaries unusable, so
# sync the library into each consumer's output directory on every build.
set(_aegis_engine_consumers
    aegis_game aegis_demo aegis_roundtrip aegis_batch_demo aegis_orbit_demo
    aegis_rocket_dump aegis_assembly_check aegis_flight_check aegis_game_check aegis_craft_check
    aegis_tests)
foreach(_consumer IN LISTS _aegis_engine_consumers)
    if(TARGET ${_consumer})
        add_custom_command(TARGET aegis_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aegis_engine> $<TARGET_FILE_DIR:${_consumer}>
            COMMENT "aegis_engine -> ${_consumer} output dir")
    endif()
endforeach()

message(STATUS "------------------------------------------------------------------")
