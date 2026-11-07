# aegis_craft_check — Milestone 10 tool: craft save/load round-trip proof.
#
# For each built-in preset it (1) serializes the assembly + staging to JSON,
# (2) loads it back into a fresh assembly, (3) compares parts, positions,
# rotation/flip, tank fill, staging assignments, mass, CoM, Izz and Δv,
# and (4) re-saves and byte-compares the two JSON documents. Headless; always
# runs. Exits non-zero on any mismatch.
add_executable(aegis_craft_check main.cpp)
target_link_libraries(aegis_craft_check PRIVATE aegis::core aegis::parts aegis::serial aegis::game_session)
aegis_set_warnings(aegis_craft_check)

if(AEGIS_BUILD_TESTS)
    add_test(NAME aegis_craft_check COMMAND aegis_craft_check)
endif()
