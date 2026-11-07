# aegis_core — no Vulkan, no windowing. OBJECT module; aggregated into aegis_engine.
add_library(aegis_core OBJECT
    src/assert.cpp
    src/log.cpp
    src/result.cpp)
aegis_engine_module(aegis_core)
