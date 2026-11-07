// Aegis2D — unit tests for the aegis_game session (Milestone 9).
#include "game_session.hpp"

#include "aegis_test.hpp"

#include <cmath>

using namespace aegis::game;
namespace core = aegis::core;
using aegis::parts::PartIndex;

namespace {


core::f32 engine_center_x(const GameSession& session) {
    return session.build_assembly().part_center_wu(0)[0];
}

}  // namespace

AEGIS_TEST(game_session, "starts in the VAB with the demo rocket loaded") {
    GameSession session;
    AEGIS_CHECK(session.screen() == GameScreen::Build);
    AEGIS_CHECK_EQ(session.part_count(), 6u);
    AEGIS_CHECK(session.build_mass_kg() > 3000.0);
    AEGIS_CHECK(session.delta_v_estimate() > 1000.0);
    AEGIS_CHECK_EQ(session.staging().stage_count(), 3u);
}

AEGIS_TEST(game_session, "demo rocket is launchable as delivered") {
    GameSession session;
    AEGIS_CHECK(session.launch_blocker().empty());
}

AEGIS_TEST(game_session, "clearing the assembly blocks launch until a pod is back") {
    GameSession session;
    session.clear_assembly();
    AEGIS_CHECK_EQ(session.part_count(), 0u);
    AEGIS_CHECK(session.launch_blocker() == "NEED A COMMAND POD");
    AEGIS_CHECK(session.delta_v_estimate() == 0.0);
    AEGIS_CHECK(!session.launch());
    AEGIS_CHECK(session.screen() == GameScreen::Build);
}

AEGIS_TEST(game_session, "placing on a compatible node grows the assembly") {
    GameSession session;
    session.select_template(1);  // Long-2
    // Pod top is at y == 7 (6 parts, 1..2 cells each): hover right on the node.
    session.set_cursor_world(0.5f, 7.0f);
    const auto& preview = session.hover_preview();
    AEGIS_CHECK(preview.valid);
    AEGIS_CHECK(session.place_selected());
    AEGIS_CHECK_EQ(session.part_count(), 7u);
}

AEGIS_TEST(game_session, "mirror symmetry places a twin on the opposite node") {
    GameSession session;
    session.select_template(9);  // Radial separator — lateral attachment
    // Short-1 lateral node, left side at world (0, 5.5).
    session.set_cursor_world(0.0f, 5.5f);  // Short-1 lateral node, left side
    const auto& preview = session.hover_preview();
    AEGIS_CHECK(preview.valid);
    session.cycle_symmetry();  // -> MirrorVertical
    const core::u32 before = session.part_count();
    AEGIS_CHECK(session.place_selected());
    AEGIS_CHECK_EQ(session.part_count(), before + 2u);  // main + twin
}

AEGIS_TEST(game_session, "no placement happens when the ghost is invalid") {
    GameSession session;
    session.select_template(1);
    session.set_cursor_world(-50.0f, -50.0f);  // far outside the rocket
    const auto& preview = session.hover_preview();
    AEGIS_CHECK(!preview.valid);
    AEGIS_CHECK(!session.place_selected());
    AEGIS_CHECK_EQ(session.part_count(), 6u);
}

AEGIS_TEST(game_session, "removal deletes the part under the cursor") {
    GameSession session;
    const core::u32 before = session.part_count();
    session.set_cursor_world(engine_center_x(session), 0.5f);  // liftoff engine
    AEGIS_CHECK(session.remove_at_cursor());
    AEGIS_CHECK_EQ(session.part_count(), before - 1u);
}

AEGIS_TEST(game_session, "launch moves to flight and fires stage zero") {
    GameSession session;
    AEGIS_CHECK(session.launch());
    AEGIS_CHECK(session.screen() == GameScreen::Flight);
    AEGIS_CHECK(session.flight() != nullptr);
    AEGIS_CHECK_EQ(session.flight()->staging().cursor(), 1u);  // stage 0 fired at launch
    AEGIS_CHECK(session.throttle() == 1.0);
}

AEGIS_TEST(game_session, "the demo rocket lifts off under full throttle") {
    GameSession session;
    AEGIS_CHECK(session.launch());
    for (int i = 0; i < 900; ++i) {  // 15 seconds at 1/60
        session.update(1.0 / 60.0);
    }
    AEGIS_CHECK(session.altitude_m() > 80.0);
    AEGIS_CHECK(session.speed_m_s() > 40.0);
    AEGIS_CHECK(session.propellant_fraction() < 1.0);  // engines burned some fuel
}

AEGIS_TEST(game_session, "abort returns to the VAB with the build intact") {
    GameSession session;
    AEGIS_CHECK(session.launch());
    session.update(0.5);
    session.abort_to_vab();
    AEGIS_CHECK(session.screen() == GameScreen::Build);
    AEGIS_CHECK(session.flight() == nullptr);
    AEGIS_CHECK_EQ(session.part_count(), 6u);  // the pre-launch VAB state is kept
}

AEGIS_TEST(game_session, "map overlay only toggles in flight") {
    GameSession session;
    session.toggle_map_overlay();
    AEGIS_CHECK(!session.map_overlay());
    AEGIS_CHECK(session.launch());
    session.toggle_map_overlay();
    AEGIS_CHECK(session.map_overlay());
    session.toggle_map_overlay();
    AEGIS_CHECK(!session.map_overlay());
}

AEGIS_TEST(game_session, "staging activates in order and reports actions") {
    GameSession session;
    AEGIS_CHECK(session.launch());  // fires stage 0 (liftoff ignition)
    AEGIS_CHECK_EQ(session.flight()->staging().cursor(), 1u);
    session.activate_stage();        // stage 1: decoupler separation
    AEGIS_CHECK_EQ(session.flight()->staging().cursor(), 2u);
    session.activate_stage();        // stage 2: upper engine ignition
    AEGIS_CHECK_EQ(session.flight()->staging().cursor(), 3u);
    session.activate_stage();        // exhausted
    AEGIS_CHECK_EQ(session.flight()->staging().cursor(), 3u);
}

AEGIS_TEST(game_session, "determinism: identical sessions fly bitwise-identical paths") {
    GameSession first;
    GameSession second;
    AEGIS_CHECK(first.launch());
    AEGIS_CHECK(second.launch());
    for (int i = 0; i < 600; ++i) {
        first.update(1.0 / 60.0);
        second.update(1.0 / 60.0);
    }
    AEGIS_CHECK(first.altitude_m() == second.altitude_m());
    AEGIS_CHECK(first.flight()->state().position.x == second.flight()->state().position.x);
    AEGIS_CHECK(first.propellant_fraction() == second.propellant_fraction());
}
