# Aegis2D — pinned third-party dependencies (FetchContent).
# Versions are locked here; bump them deliberately, never float them.
#
#   Vulkan-Headers v1.3.295   — API headers (no SDK install required)
#   volk          1.3.295     — Vulkan loader bootstrap (loaded via dlopen at runtime)
#   glfw          3.4         — windowing (optional; Null platform fallback when X11/Wayland absent)
#   glslang       15.0.0      — build-time GLSL -> SPIR-V compiler (validator binary only; not linked)
#   miniz         3.0.2       — DEFLATE/CRC-32 for the PNG writer (Milestone 5)
#
# FetchContent_Populate is used (instead of MakeAvailable) for projects whose
# CMake config we don't want to polluting the build; the policy floor is clamped
# for glfw 3.4 on CMake >= 4 in the root CMakeLists.

set(CMAKE_POLICY_VERSION_MINIMUM "3.5")  # glfw 3.4 declares cmake_minimum_required(VERSION 3.4)

set(AEGIS_VULKAN_HEADERS_TAG "v1.3.295" CACHE STRING "Vulkan-Headers git tag")
set(AEGIS_VOLK_TAG           "1.3.295"  CACHE STRING "volk git tag")
set(AEGIS_GLFW_TAG           "3.4"      CACHE STRING "glfw git tag")
set(AEGIS_GLSLANG_TAG        "15.0.0"   CACHE STRING "glslang git tag (build-time shader compiler)")
set(AEGIS_MINIZ_TAG          "3.0.2"    CACHE STRING "miniz git tag (PNG writer deflate)")

if(POLICY CMP0168)
    cmake_policy(SET CMP0168 OLD)  # FetchContent_Populate is deprecated in CMake >= 3.30
endif()
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# --------------------------------------------------------------------------
# Vulkan-Headers (v1.3.295) — header-only, header include dir exported for volk.
# --------------------------------------------------------------------------
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        ${AEGIS_VULKAN_HEADERS_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(VulkanHeaders)

# --------------------------------------------------------------------------
# volk (1.3.295) — static Vulkan loader wrapper. Populate + add_subdirectory so
# we control the target properties; alias as aegis::volk.
# --------------------------------------------------------------------------
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        ${AEGIS_VOLK_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_Populate(volk)
FetchContent_GetProperties(volk)

if(NOT TARGET volk)
    add_subdirectory(${volk_SOURCE_DIR} ${volk_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

if(TARGET volk)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
endif()
if(NOT TARGET aegis::volk)
    add_library(aegis::volk ALIAS volk)
endif()

# --------------------------------------------------------------------------
# glfw (3.4) — windowing. Only built when AEGIS_WINDOWED=ON.
# X11/Wayland are auto-detected; when neither is present glfw falls back to
# its Null platform (everything compiles, the demo runs as a bounded smoke
# test, and no window is created).
# --------------------------------------------------------------------------
if(AEGIS_WINDOWED)
    find_package(PkgConfig QUIET)

    set(AEGIS_GLFW_NULL_PLATFORM 0)
    set(_aegis_have_x11 OFF)
    set(_aegis_have_wayland OFF)

    if(PkgConfig_FOUND)
        pkg_check_modules(X11 QUIET x11)
        pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
        if(X11_FOUND)
            set(_aegis_have_x11 ON)
        endif()
        if(WAYLAND_CLIENT_FOUND)
            set(_aegis_have_wayland ON)
        endif()
    endif()

    # Windows (Win32) and macOS (Cocoa) always have a native desktop platform —
    # pkg-config detection never applies there, and silently forcing the Null
    # platform would starve the Vulkan instance of VK_KHR_surface /
    # VK_KHR_win32_surface (glfwGetRequiredInstanceExtensions returns nothing),
    # making glfwCreateWindowSurface fail with VK_ERROR_EXTENSION_NOT_PRESENT.
    if(WIN32 OR APPLE)
        set(AEGIS_GLFW_NULL_PLATFORM 0)
        if(WIN32)
            set(GLFW_BUILD_WIN32     ON CACHE BOOL "" FORCE)
            set(GLFW_BUILD_COCOA     OFF CACHE BOOL "" FORCE)
        else()
            set(GLFW_BUILD_COCOA     ON CACHE BOOL "" FORCE)
            set(GLFW_BUILD_WIN32     OFF CACHE BOOL "" FORCE)
        endif()
        set(GLFW_BUILD_X11      OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_NULL     OFF CACHE BOOL "" FORCE)
        message(STATUS "Aegis2D: native desktop platform — building glfw with the ${CMAKE_SYSTEM_NAME} backend")
    elseif(NOT _aegis_have_x11 AND NOT _aegis_have_wayland)
        set(AEGIS_GLFW_NULL_PLATFORM 1)
        set(GLFW_BUILD_NULL     ON  CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11      OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
        message(STATUS "Aegis2D: X11/Wayland dev packages not found — building glfw with the Null platform (windowed apps run as bounded smoke tests)")
    else()
        set(AEGIS_GLFW_NULL_PLATFORM 0)
        set(GLFW_BUILD_NULL     OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11      ${_aegis_have_x11}   CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND  ${_aegis_have_wayland} CACHE BOOL "" FORCE)
    endif()

    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    set(GLFW_VULKAN_STATIC  OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        ${AEGIS_GLFW_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_Populate(glfw)
    add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR} EXCLUDE_FROM_ALL)

    if(NOT TARGET aegis::glfw)
        add_library(aegis::glfw ALIAS glfw)
    endif()
endif()

# --------------------------------------------------------------------------
# miniz (3.0.2) — single-purpose DEFLATE/CRC-32 used by the PNG writer.
# Compiles four C files from the pinned tag; no zlib dependency.
# --------------------------------------------------------------------------
FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        ${AEGIS_MINIZ_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_Populate(miniz)

if(NOT TARGET aegis_miniz)
    # miniz 3.x expects a generated miniz_export.h; static build => empty MINIZ_EXPORT
    # (same recipe as the upstream meson build).
    set(AEGIS_MINIZ_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/aegis_miniz_gen")
    file(WRITE "${AEGIS_MINIZ_GEN_DIR}/miniz_export.h"
         "#pragma once\n#ifndef MINIZ_EXPORT\n#define MINIZ_EXPORT\n#endif\n")
    add_library(aegis_miniz STATIC
        ${miniz_SOURCE_DIR}/miniz.c
        ${miniz_SOURCE_DIR}/miniz_zip.c
        ${miniz_SOURCE_DIR}/miniz_tinfl.c
        ${miniz_SOURCE_DIR}/miniz_tdef.c)
    target_include_directories(aegis_miniz PUBLIC ${miniz_SOURCE_DIR} ${AEGIS_MINIZ_GEN_DIR})
endif()
if(NOT TARGET aegis::miniz)
    add_library(aegis::miniz ALIAS aegis_miniz)
endif()

# --------------------------------------------------------------------------
# glslang (15.0.0) — build-time GLSL -> SPIR-V compiler (glslangValidator binary
# only). NOT linked into the engine; used via custom commands to produce .spv
# artifacts. Lean configuration: no HLSL frontend, no SPIRV-Tools optimizer.
# 15.0.0 is the first release whose SPIRV/SpvBuilder.h includes <cstdint>
# (required by newer GCC/MinGW); a scoped force-include is added on top as
# belt and braces for sibling headers — it is restored immediately afterwards
# and never leaks into first-party targets.
# --------------------------------------------------------------------------
set(ENABLE_HLSL           OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER    OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT            OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_NVTT           OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        ${AEGIS_GLSLANG_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_Populate(glslang)
set(AEGIS_GLSLANG_OLD_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FIcstdint")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdint")
endif()
add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR} EXCLUDE_FROM_ALL)
set(CMAKE_CXX_FLAGS "${AEGIS_GLSLANG_OLD_CXX_FLAGS}")
