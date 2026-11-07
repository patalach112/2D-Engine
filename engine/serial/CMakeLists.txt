# aegis_serial — strict JSON subset + craft save/load (Milestone 10).
add_library(aegis_serial OBJECT
    src/json.cpp
    src/craft.cpp
    src/craft_store.cpp)
aegis_engine_module(aegis_serial)
