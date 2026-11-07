# aegis_assembly_check — Milestone 7 tool: builds a deterministic three-stage
# rocket with radial boosters, stages it end to end, and verifies the assembly
# machinery (snapping, symmetry, split, mass/CoM). Headless; always runs.
add_executable(aegis_assembly_check main.cpp)
target_link_libraries(aegis_assembly_check PRIVATE aegis::core aegis::parts)
aegis_set_warnings(aegis_assembly_check)

if(AEGIS_BUILD_TESTS)
    add_test(NAME aegis_assembly_check COMMAND aegis_assembly_check)
endif()
