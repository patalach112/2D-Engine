# aegis_flight_check — Milestone 8 tool: deterministic three-stage ascent on an
# Earth-like body; verifies ignition order, separation delta-v, propellant
# routing/conservation, detached-stage ballistics and the flight state machine.
add_executable(aegis_flight_check main.cpp)
target_link_libraries(aegis_flight_check PRIVATE aegis::core aegis::parts aegis::physics)
aegis_set_warnings(aegis_flight_check)

if(AEGIS_BUILD_TESTS)
    add_test(NAME aegis_flight_check COMMAND aegis_flight_check)
endif()
