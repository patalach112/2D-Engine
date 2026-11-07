# Aegis2D — engine modules build as OBJECT libraries and are aggregated into
# ONE shared library (aegis_engine). Include this after project(); it expects
# AEGIS_ENGINE_INCLUDE_DIRS to list every engine module's include directory
# and sets the global PIC flag required to link the static third-party
# dependencies into a shared object.

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Global include path list — engine sources include each other freely.
set(AEGIS_ENGINE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/memory/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/vk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/orbit/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/parts/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/serial/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/physics/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/render/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/renderer2d/include
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/offscreen/include)

# Applies the shared contract to one engine module: PIC, private symbol
# visibility, and the AEGIS_ENGINE_BUILDING define (dllexport on Windows).
function(aegis_engine_module target)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${target} PRIVATE AEGIS_ENGINE_BUILDING=1)
    target_include_directories(${target} PRIVATE ${AEGIS_ENGINE_INCLUDE_DIRS})
    target_link_libraries(${target} PRIVATE aegis::volk)  # volk.h appears in public headers
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
    endif()
    aegis_set_warnings(${target})
endfunction()
