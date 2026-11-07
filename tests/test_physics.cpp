// Aegis2D — M8 physics module tests: model kernels, VehicleFlight state
// machine, propellant drain, contact constraint, separation impulse bookkeeping,
// determinism and the Kepler oracle round trip.
#include <aegis/orbit/orbit.hpp>
#include <aegis/physics/model.hpp>
#include <aegis/physics/vehicle_flight.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "aegis_test.hpp"

using aegis::core::f64;
using aegis::core::f32;
namespace core = aegis::core;

#define EXPECT_PHYS_OK(res)                                                                     \
    do {                                                                                        \
        if (!(res).has_value()) {                                                               \
            ::aegis::test::report_failure(__FILE__, __LINE__,                                   \
                                          std::string("unexpected failure: ") +                  \
                                          (res).error().message);                               \
        }                                                                                       \
    } while (0)

namespace {

using aegis::parts::PartIndex;
using aegis::parts::StagingList;
using aegis::parts::TemplateId;
using aegis::parts::VehicleAssembly;
using aegis::physics::AeroConfig;
using aegis::physics::CelestialBody;
using aegis::physics::ContactConfig;
using aegis::physics::DetachedStage;
using aegis::physics::FlightEvent;
using aegis::physics::FlightState;
using aegis::physics::RotationConfig;
using aegis::physics::VehicleConfig;
using aegis::physics::VehicleFlight;
using aegis::physics::Vec2d;
using aegis::physics::ContactState;

constexpr TemplateId kPod = 0;
constexpr TemplateId kShortTank = 3;
constexpr TemplateId kSparrow = 4;
constexpr TemplateId kKestrel = 6;
constexpr TemplateId kDecoupler = 7;
constexpr TemplateId kRadialSeparator = 9;
constexpr TemplateId kNoseFairing = 11;

constexpr f64 kHalfPi = 1.5707963267948966;

// A synthetic vacuum body: mu == 0 removes gravity entirely for ballistic
// Δv checks; the radius keeps coordinates finite and is never touched.
VehicleConfig airless_config(f64 mu = 0.0) {
    VehicleConfig config;
    config.body.mu = mu;
    config.body.radius = 0.5;
    config.body.atmosphere_density0 = 0.0;
    config.body.atmosphere_scale_height = 1.0;
    config.body.atmosphere_top = 0.0;
    config.aero.drag_coefficient = 0.0;
    config.aero.reference_area = 1.0;
    return config;
}

// Synthetic Earth-like body with a thin atmosphere (density at surface ro).
VehicleConfig earth_config() {
    VehicleConfig config;
    config.body.mu = 1.0;
    config.body.radius = 0.5;
    config.body.atmosphere_density0 = 1.0;
    config.body.atmosphere_scale_height = 8.5;
    config.body.atmosphere_top = 100.0;
    config.aero.drag_coefficient = 0.0;
    config.aero.reference_area = 1.0;
    return config;
}

bool near(f64 actual, f64 expected, f64 eps) noexcept {
    return std::fabs(actual - expected) <= eps;
}

struct DemoRocketPlanes {
    VehicleAssembly asmbl;
    PartIndex liftoff = 0, sep1 = 0, kestrel = 0, tank2 = 0, sep2 = 0, tank3 = 0, pod = 0,
              fairing = 0, sep_left = 0, booster_left_tank = 0, booster_left_engine = 0,
              sep_right = 0, booster_right_tank = 0, booster_right_engine = 0;
};

DemoRocketPlanes build_demo_rocket() {
    DemoRocketPlanes r;
    auto add = [&r](TemplateId tmpl, aegis::core::i16 gx, aegis::core::i16 gy, aegis::core::u8 rot) {
        core::Result<PartIndex> res = r.asmbl.add_root(tmpl, gx, gy, rot);
        return res.value();
    };
    auto snap = [&r](PartIndex parent, aegis::parts::NodeId pnode, TemplateId child,
                     aegis::parts::NodeId cnode, aegis::core::u8 rot) {
        core::Result<PartIndex> res = r.asmbl.snap(parent, pnode, child, cnode, rot);
        return res.value();
    };
    r.liftoff = add(kSparrow, 0, 0, 0);
    r.sep1 = snap(r.liftoff, 0, kDecoupler, 1, 0);
    r.kestrel = snap(r.sep1, 0, kKestrel, 1, 0);
    r.tank2 = snap(r.kestrel, 0, kShortTank, 1, 0);
    r.sep2 = snap(r.tank2, 0, kDecoupler, 1, 0);
    r.tank3 = snap(r.sep2, 0, kShortTank, 1, 0);
    r.pod = snap(r.tank3, 0, kPod, 0, 0);
    r.fairing = snap(r.pod, 3, kNoseFairing, 0, 0);
    r.sep_left = snap(r.tank2, 2, kRadialSeparator, 1, 0);
    r.booster_left_tank = snap(r.sep_left, 0, kShortTank, 3, 0);
    r.booster_left_engine = snap(r.booster_left_tank, 1, kSparrow, 0, 0);
    core::Result<PartIndex> sep_right =
        r.asmbl.mirror_snap(r.tank2, 2, kRadialSeparator, 1, 0, aegis::parts::Axis::Vertical, 0.5f);
    r.sep_right = sep_right.value();
    r.booster_right_tank = snap(r.sep_right, 0, kShortTank, 2, 0);
    r.booster_right_engine = snap(r.booster_right_tank, 1, kSparrow, 0, 0);
    return r;
}

// The five-stage flight plan from aegis_flight_check / the M7 demo rocket.
StagingList flight_plan(const DemoRocketPlanes& r, const VehicleAssembly& asmbl) {
    StagingList staging;
    const auto assign = [&staging, &asmbl](const PartIndex part, const aegis::core::u8 stage) {
        core::Result<void> res = staging.assign(asmbl, part, stage);
        EXPECT_PHYS_OK(res);
    };
    assign(r.liftoff, 0);
    assign(r.booster_left_engine, 0);
    assign(r.booster_right_engine, 0);
    assign(r.sep1, 1);
    assign(r.sep_left, 1);
    assign(r.sep_right, 1);
    assign(r.kestrel, 2);
    assign(r.sep2, 3);
    assign(r.fairing, 4);
    return staging;
}

VehicleFlight make_flight(DemoRocketPlanes& r, const VehicleConfig& config, const Vec2d position,
                          const Vec2d velocity, f64 heading = kHalfPi) {
    StagingList staging = flight_plan(r, r.asmbl);
    core::Result<VehicleFlight> flight =
        VehicleFlight::create(std::move(r.asmbl), std::move(staging), config, position, velocity, heading);
    EXPECT_PHYS_OK(flight);
    return std::move(flight.value());  // NOLINT — moved into the caller's optional-like wrapper
}

}  // namespace

// ---------------------------------------------------------------------------
// Model kernels
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "gravity falls off as inverse square toward the planet centre") {
    const CelestialBody body{.mu = 2.0, .radius = 1.0, .atmosphere_density0 = 0.0,
                             .atmosphere_scale_height = 1.0, .atmosphere_top = 0.0};
    const Vec2d g = aegis::physics::model::gravity_acceleration(body, Vec2d{0.0, 3.0});
    AEGIS_CHECK(near(g.x, 0.0, 1e-12));
    AEGIS_CHECK(near(g.y, -2.0 / 9.0, 1e-12));
    const Vec2d g2 = aegis::physics::model::gravity_acceleration(body, Vec2d{4.0, 0.0});
    AEGIS_CHECK(near(g2.x, -2.0 / 16.0, 1e-12));
}

AEGIS_TEST(physics, "gravity vanishes for a mu == 0 body") {
    const CelestialBody body{.mu = 0.0, .radius = 1.0, .atmosphere_density0 = 0.0,
                             .atmosphere_scale_height = 1.0, .atmosphere_top = 0.0};
    const Vec2d g = aegis::physics::model::gravity_acceleration(body, Vec2d{7.0, 11.0});
    AEGIS_CHECK(g.x == 0.0 && g.y == 0.0);
}

AEGIS_TEST(physics, "atmosphere density is exponential and exactly zero above the top") {
    const CelestialBody body{.mu = 1.0, .radius = 100.0, .atmosphere_density0 = 1.225,
                             .atmosphere_scale_height = 8500.0, .atmosphere_top = 20000.0};
    AEGIS_CHECK(near(aegis::physics::model::atmosphere_density(body, 0.0), 1.225, 1e-15));
    AEGIS_CHECK(near(aegis::physics::model::atmosphere_density(body, 8500.0), 1.225 / 2.718281828459045, 1e-9));
    AEGIS_CHECK(aegis::physics::model::atmosphere_density(body, 20000.0) == 0.0);
    AEGIS_CHECK(aegis::physics::model::atmosphere_density(body, 50000.0) == 0.0);
}

AEGIS_TEST(physics, "drag opposes motion and scales with velocity squared") {
    const CelestialBody body{.mu = 1.0, .radius = 1.0, .atmosphere_density0 = 1.225,
                             .atmosphere_scale_height = 8500.0, .atmosphere_top = 1e9};
    AeroConfig aero;
    const Vec2d v1{10.0, 0.0};
    const Vec2d v2{20.0, 0.0};
    const Vec2d f1 = aegis::physics::model::drag_force(body, 1.0, v1, aero);
    const Vec2d f2 = aegis::physics::model::drag_force(body, 1.0, v2, aero);
    AEGIS_CHECK(f1.x < 0.0 && near(f1.y, 0.0, 1e-15));
    AEGIS_CHECK(near(f2.x / f1.x, 4.0, 1e-12));
    const Vec2d f0 = aegis::physics::model::drag_force(body, 1.0, Vec2d{}, aero);
    AEGIS_CHECK(f0.x == 0.0 && f0.y == 0.0);
}

AEGIS_TEST(physics, "wrap_pi normalizes to (-pi, pi]") {
    AEGIS_CHECK(near(aegis::physics::model::wrap_pi(0.5), 0.5, 1e-15));
    AEGIS_CHECK(near(aegis::physics::model::wrap_pi(3.0 * kHalfPi), -kHalfPi, 1e-12));
    AEGIS_CHECK(near(aegis::physics::model::wrap_pi(-3.0 * kHalfPi), kHalfPi, 1e-12));
}

AEGIS_TEST(physics, "engine thrust axis rotates with the part rotation") {
    const aegis::parts::PartTemplate& sparrow = aegis::parts::templates()[kSparrow];
    const Vec2d axis0 = aegis::physics::model::engine_thrust_local_axis(sparrow, 0);
    AEGIS_CHECK(near(axis0.x, 0.0, 1e-6) && near(axis0.y, 1.0, 1e-6));
    const Vec2d axis2 = aegis::physics::model::engine_thrust_local_axis(sparrow, 2);
    AEGIS_CHECK(near(axis2.x, 0.0, 1e-6) && near(axis2.y, -1.0, 1e-6));
    const Vec2d axis1 = aegis::physics::model::engine_thrust_local_axis(sparrow, 1);
    AEGIS_CHECK(near(axis1.x, -1.0, 1e-6) && near(axis1.y, 0.0, 1e-6));
    const Vec2d axis3 = aegis::physics::model::engine_thrust_local_axis(sparrow, 3);
    AEGIS_CHECK(near(axis3.x, 1.0, 1e-6) && near(axis3.y, 0.0, 1e-6));
}

// ---------------------------------------------------------------------------
// Creation / validation
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "create rejects an invalid config") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig bad = airless_config(0.0);
    bad.body.mu = -1.0;
    StagingList staging = flight_plan(r, r.asmbl);
    core::Result<VehicleFlight> flight =
        VehicleFlight::create(std::move(r.asmbl), std::move(staging), bad, Vec2d{0.0, 100.0}, Vec2d{});
    AEGIS_CHECK(!flight.has_value());
}

AEGIS_TEST(physics, "set_throttle and set_pitch_input are clamped") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{0.0, 100.0}, Vec2d{});
    flight.set_throttle(2.0);
    AEGIS_CHECK(flight.throttle() == 1.0);
    flight.set_throttle(-0.5);
    AEGIS_CHECK(flight.throttle() == 0.0);
    flight.set_pitch_input(3.0);
    AEGIS_CHECK(flight.pitch_input() == 1.0);
    flight.set_pitch_input(-2.0);
    AEGIS_CHECK(flight.pitch_input() == -1.0);
}

// ---------------------------------------------------------------------------
// Propulsion / propellant
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "a burn drains propellant at the analytic rate") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{0.0, 100.0}, Vec2d{});
    // Minimal plan: stage 0 only (three engines).
    flight.set_throttle(1.0);
    EXPECT_PHYS_OK(flight.activate_next_stage());  // ignitions only
    const f64 mass_initial = static_cast<f64>(flight.mass_properties().total_mass);
    for (int i = 0; i < 240; ++i) {  // exactly 1 second at 1/240
        flight.tick(VehicleFlight::kFixedDt);
    }
    const f64 expected_rate = 3.0 * 60000.0 / (295.0 * aegis::physics::kStandardGravity);  // kg/s
    const f64 drained = mass_initial - static_cast<f64>(flight.mass_properties().total_mass);
    AEGIS_CHECK(near(drained, expected_rate * 1.0, 1e-2));
}

AEGIS_TEST(physics, "all four tanks drain in proportion (component-wide pool)") {
    DemoRocketPlanes r = build_demo_rocket();
    const PartIndex tank2 = r.tank2, tank3 = r.tank3, bl = r.booster_left_tank, br = r.booster_right_tank;
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{0.0, 100.0}, Vec2d{});
    flight.set_throttle(1.0);
    EXPECT_PHYS_OK(flight.activate_next_stage());  // liftoff ignition
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    const f64 remaining = flight.assembly().part(tank2)->propellant;
    AEGIS_CHECK(near(static_cast<f64>(flight.assembly().part(tank3)->propellant), remaining, 1e-3));
    AEGIS_CHECK(near(static_cast<f64>(flight.assembly().part(bl)->propellant), remaining, 1e-3));
    AEGIS_CHECK(near(static_cast<f64>(flight.assembly().part(br)->propellant), remaining, 1e-3));
}

AEGIS_TEST(physics, "tsiolkovsky delta-v matches the rocket equation within 0.5%") {
    // Pod + Sparrow + Short tank on a gravity-free body: pure burn.
    VehicleAssembly asmbl;
    core::Result<PartIndex> engine = asmbl.add_root(kSparrow, 0, 0, 0);
    core::Result<PartIndex> tank = asmbl.snap(engine.value(), 0, kShortTank, 1, 0);
    core::Result<PartIndex> pod = asmbl.snap(tank.value(), 0, kPod, 0, 0);
    StagingList staging;
    EXPECT_PHYS_OK(staging.assign(asmbl, engine.value(), 0));
    core::Result<VehicleFlight> flight_res =
        VehicleFlight::create(std::move(asmbl), std::move(staging), airless_config(0.0),
                              Vec2d{0.0, 100.0}, Vec2d{}, kHalfPi);
    EXPECT_PHYS_OK(flight_res);
    VehicleFlight flight = std::move(flight_res.value());
    flight.set_throttle(1.0);
    EXPECT_PHYS_OK(flight.activate_next_stage());
    const f64 m0 = flight.mass_properties().total_mass;
    for (int i = 0; i < 240 * 60; ++i) {  // burn well past the 20 s flight time of the tank
        flight.tick(VehicleFlight::kFixedDt);
    }
    const f64 mf = static_cast<f64>(flight.mass_properties().total_mass);
    const f64 expected = aegis::physics::kStandardGravity * 295.0 * std::log(m0 / mf);
    AEGIS_CHECK(near(flight.state().velocity.length(), expected, expected * 0.005));
    AEGIS_CHECK(near(mf, 1190.0, 1e-6));  // pod + dry engine + dry tank
}

AEGIS_TEST(physics, "engine emits exactly one flameout and then coasts") {
    VehicleAssembly asmbl;
    core::Result<PartIndex> engine = asmbl.add_root(kSparrow, 0, 0, 0);
    core::Result<PartIndex> pod = asmbl.snap(engine.value(), 0, kPod, 0, 0);
    StagingList staging;
    EXPECT_PHYS_OK(staging.assign(asmbl, engine.value(), 0));
    core::Result<VehicleFlight> flight_res =
        VehicleFlight::create(std::move(asmbl), std::move(staging), airless_config(0.0),
                              Vec2d{0.0, 100.0}, Vec2d{}, kHalfPi);
    EXPECT_PHYS_OK(flight_res);
    VehicleFlight flight = std::move(flight_res.value());
    flight.set_throttle(1.0);
    EXPECT_PHYS_OK(flight.activate_next_stage());
    for (int i = 0; i < 240 * 30; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    // No tank: the tankless engine flames out on the first drain tick.
    std::vector<FlightEvent> events;
    flight.take_events(events);
    const auto flameouts = std::count(events.begin(), events.end(), FlightEvent::EngineFlameout);
    AEGIS_CHECK_EQ(flameouts, 1);
    const Vec2d coast_velocity = flight.state().velocity;
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.state().velocity == coast_velocity);
}

// ---------------------------------------------------------------------------
// Staging / separation
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "firing stage 1 splits the rocket and keeps total momentum") {
    DemoRocketPlanes r = build_demo_rocket();
    const f64 velocity0 = 100.0;
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{0.0, 100.0}, Vec2d{30.0, velocity0});
    EXPECT_PHYS_OK(flight.activate_next_stage());  // stage 0: ignitions
    EXPECT_PHYS_OK(flight.activate_next_stage());  // stage 1: three separators
    const f64 m_veh = static_cast<f64>(flight.mass_properties().total_mass);
    const Vec2d p_vehicle = flight.state().velocity * m_veh;
    Vec2d p_total = p_vehicle;
    for (const DetachedStage& stage : flight.detached_stages()) {
        const f64 m = static_cast<f64>(stage.assembly.mass_properties().total_mass);
        p_total += stage.state.velocity * m;
    }
    // Initial momentum (engines do not burn yet: throttle == 0).
    const Vec2d p_before{30.0 * 4143.0, velocity0 * 4143.0};
    AEGIS_CHECK(near(p_total.x, p_before.x, 1e-4));
    AEGIS_CHECK(near(p_total.y, p_before.y, 1e-4));
}

AEGIS_TEST(physics, "material points keep their world positions across a split") {
    DemoRocketPlanes r = build_demo_rocket();
    const PartIndex pod = r.pod;
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{12.0, 100.0}, Vec2d{30.0, 40.0});
    // heading == pi/2 -> world offset == grid offset (theta == 0).
    const aegis::parts::MassProperties mp_before = flight.mass_properties();
    const Vec2d com_before{static_cast<f64>(mp_before.center_of_mass[0]),
                           static_cast<f64>(mp_before.center_of_mass[1])};
    const auto pod_center = flight.assembly().part_center_wu(pod);
    const Vec2d pod_grid{static_cast<f64>(pod_center[0]), static_cast<f64>(pod_center[1])};
    const Vec2d pod_world_before = flight.state().position + (pod_grid - com_before);
    EXPECT_PHYS_OK(flight.activate_next_stage());  // stage 0
    EXPECT_PHYS_OK(flight.activate_next_stage());  // stage 1
    const aegis::parts::MassProperties mp_after = flight.mass_properties();
    const Vec2d com_after{static_cast<f64>(mp_after.center_of_mass[0]),
                          static_cast<f64>(mp_after.center_of_mass[1])};
    const Vec2d pod_world_after = flight.state().position + (pod_grid - com_after);
    AEGIS_CHECK(near(pod_world_after.x, pod_world_before.x, 1e-9));
    AEGIS_CHECK(near(pod_world_after.y, pod_world_before.y, 1e-9));
}

AEGIS_TEST(physics, "detached stages are tracked as ballistic free-flyers") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig config = airless_config(1.0);  // real gravity, r >> R
    VehicleFlight flight = make_flight(r, config, Vec2d{0.0, 10000.0}, Vec2d{0.0, 50.0});
    EXPECT_PHYS_OK(flight.activate_next_stage());  // stage 0
    EXPECT_PHYS_OK(flight.activate_next_stage());  // stage 1
    AEGIS_CHECK_EQ(flight.detached_stages().size(), 3u);
    const Vec2d initial = flight.detached_stages().front().state.velocity;
    for (int i = 0; i < 240; ++i) {  // 1 s of ballistic flight
        flight.tick(VehicleFlight::kFixedDt);
    }
    const DetachedStage& stage = flight.detached_stages().front();
    const f64 elapsed = stage.state.time;
    AEGIS_CHECK(near(elapsed, 1.0, 1e-12));
    // Acceleration magnitude matches mu/r^2 at the stage's current radius.
    const f64 r_cur = stage.state.position.length();
    const f64 expected_g = 1.0 / (r_cur * r_cur);
    const Vec2d dv = stage.state.velocity - initial;
    const f64 measured_g = dv.length() / elapsed;
    AEGIS_CHECK(near(measured_g, expected_g, expected_g * 0.02));
}

AEGIS_TEST(physics, "the full five-stage sequence produces five tracked stages and a clean assembly") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{0.0, 100.0}, Vec2d{0.0, 0.0});
    const f64 mass_initial = static_cast<f64>(flight.mass_properties().total_mass);
    for (int stage = 0; stage < 5; ++stage) {
        EXPECT_PHYS_OK(flight.activate_next_stage());
    }
    AEGIS_CHECK(flight.assembly().validate().empty());
    AEGIS_CHECK_EQ(flight.detached_stages().size(), 5u);  // 4 boosters/stages + fairing
    AEGIS_CHECK_EQ(flight.assembly().alive_count(), 2u);  // pod + tank3
    // Mass is conserved exactly: vehicle + every tracked detached piece ==
    // the original rocket (fairings ride out as ballistic free-flyers now).
    f64 mass_after = static_cast<f64>(flight.mass_properties().total_mass);
    for (const DetachedStage& stage : flight.detached_stages()) {
        mass_after += static_cast<f64>(stage.assembly.mass_properties().total_mass);
    }
    AEGIS_CHECK(near(mass_after, mass_initial, 1e-3));
    // The fairing is a ballistic free-flyer carrying only the nose cone.
    const DetachedStage& fairing_stage = flight.detached_stages().back();
    AEGIS_CHECK_EQ(fairing_stage.assembly.alive_count(), 1u);
    AEGIS_CHECK(near(static_cast<f64>(fairing_stage.assembly.mass_properties().total_mass), 32.0, 1e-3));
    AEGIS_CHECK(flight.assembly().part(r.fairing) == nullptr);  // tombstoned in the vehicle
    // Firing a sixth time fails cleanly.
    const core::Result<void> extra = flight.activate_next_stage();
    AEGIS_CHECK(!extra.has_value());
}

AEGIS_TEST(physics, "the staging sequence emits the expected event counts") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleFlight flight = make_flight(r, airless_config(0.0), Vec2d{0.0, 100.0}, Vec2d{0.0, 0.0});
    std::vector<FlightEvent> events;
    for (int stage = 0; stage < 5; ++stage) {
        EXPECT_PHYS_OK(flight.activate_next_stage());
    }
    flight.take_events(events);
    const auto count = [&events](FlightEvent event) {
        return std::count(events.begin(), events.end(), event);
    };
    AEGIS_CHECK_EQ(count(FlightEvent::EngineIgnited), 4);   // 3 sparrows + 1 kestrel
    AEGIS_CHECK_EQ(count(FlightEvent::StageSeparated), 4);  // sep1 + 2 radials + sep2
    AEGIS_CHECK_EQ(count(FlightEvent::FairingJettisoned), 1);
    AEGIS_CHECK_EQ(count(FlightEvent::EngineFlameout), 0);
    AEGIS_CHECK_EQ(count(FlightEvent::Touchdown), 0);
    AEGIS_CHECK_EQ(count(FlightEvent::Impact), 0);
    // The event list is drained on take.
    flight.take_events(events);
    AEGIS_CHECK(events.empty());
}

// ---------------------------------------------------------------------------
// Contact
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "a vehicle resting on the surface settles Grounded with zero radial velocity") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig config = airless_config(1.0);
    VehicleFlight flight = make_flight(r, config, Vec2d{0.0, 0.5}, Vec2d{});
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.contact() == ContactState::Grounded);
    AEGIS_CHECK(near(flight.state().position.length(), 0.5, 1e-12));
    AEGIS_CHECK(flight.state().velocity.y == 0.0);
    std::vector<FlightEvent> events;
    flight.take_events(events);
    AEGIS_CHECK_EQ(std::count(events.begin(), events.end(), FlightEvent::Touchdown), 1);
}

AEGIS_TEST(physics, "a fast radial descent triggers Impact and freezes the flight") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig config = airless_config(1.0);
    VehicleFlight flight = make_flight(r, config, Vec2d{0.0, 0.6}, Vec2d{0.0, -20.0});
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
        if (flight.contact() == ContactState::Impacted) {
            break;
        }
    }
    AEGIS_CHECK(flight.contact() == ContactState::Impacted);
    AEGIS_CHECK(flight.finished());
    std::vector<FlightEvent> events;
    flight.take_events(events);
    AEGIS_CHECK_EQ(std::count(events.begin(), events.end(), FlightEvent::Impact), 1);
    const f64 frozen_time = flight.state().time;
    flight.tick(VehicleFlight::kFixedDt);
    AEGIS_CHECK(flight.state().time == frozen_time);
}

// ---------------------------------------------------------------------------
// Rotation
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "weathercock torque turns the nose toward the velocity vector") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig config = earth_config();
    config.body.mu = 0.0;  // remove gravity; rotation is what is under test
    config.aero.weathercock_gain = 10.0;
    config.aero.weathercock_arm = 2.0;
    VehicleFlight flight = make_flight(r, config, Vec2d{0.0, 10.0}, Vec2d{0.0, 10.0}, kHalfPi + 0.3);
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.state().angular_velocity < 0.0);
    AEGIS_CHECK(flight.state().heading < kHalfPi + 0.3);
}

AEGIS_TEST(physics, "zero weathercock gain leaves heading untouched") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig config = earth_config();
    config.body.mu = 0.0;
    config.aero.weathercock_gain = 0.0;
    VehicleFlight flight = make_flight(r, config, Vec2d{0.0, 10.0}, Vec2d{0.0, 10.0}, kHalfPi + 0.3);
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.state().angular_velocity == 0.0);
}

AEGIS_TEST(physics, "control torque responds to the pitch input and angular damping decays the spin") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleConfig config = airless_config(0.0);
    config.rotation.control_torque = 50.0;
    config.rotation.angular_damping = 1.0;
    VehicleFlight flight = make_flight(r, config, Vec2d{0.0, 10.0}, Vec2d{});
    flight.set_pitch_input(1.0);
    for (int i = 0; i < 120; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    const f64 spun = flight.state().angular_velocity;
    AEGIS_CHECK(spun > 0.0);
    flight.set_pitch_input(0.0);
    for (int i = 0; i < 240; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.state().angular_velocity < spun);
    AEGIS_CHECK(flight.state().angular_velocity >= 0.0);
}

// ---------------------------------------------------------------------------
// Integration semantics
// ---------------------------------------------------------------------------

AEGIS_TEST(physics, "advance executes whole fixed steps and carries the remainder") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleFlight flight_a = make_flight(r, airless_config(1.0), Vec2d{0.0, 10.0}, Vec2d{5.0, 5.0});
    DemoRocketPlanes r2 = build_demo_rocket();
    VehicleFlight flight_b = make_flight(r2, airless_config(1.0), Vec2d{0.0, 10.0}, Vec2d{5.0, 5.0});
    const f64 dt = VehicleFlight::kFixedDt;
    flight_a.advance(3.0 * dt + 0.5 * dt);  // never loops the accumulator below kFixedDt
    for (int i = 0; i < 3; ++i) {
        flight_b.tick(dt);
    }
    AEGIS_CHECK(flight_a.state() == flight_b.state());
}

AEGIS_TEST(physics, "two identical flights evolve bitwise identically") {
    DemoRocketPlanes r = build_demo_rocket();
    VehicleFlight a = make_flight(r, airless_config(1.0), Vec2d{0.0, 10.0}, Vec2d{5.0, 5.0});
    DemoRocketPlanes r2 = build_demo_rocket();
    VehicleFlight b = make_flight(r2, airless_config(1.0), Vec2d{0.0, 10.0}, Vec2d{5.0, 5.0});
    for (int i = 0; i < 300; ++i) {
        a.tick(VehicleFlight::kFixedDt);
        b.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(a.state() == b.state());
    AEGIS_CHECK(a.state().time == b.state().time);
}

AEGIS_TEST(physics, "a ballistic coast matches the Kepler oracle from elements_from_state") {
    VehicleConfig config = airless_config(1.0);
    config.body.radius = 0.5;
    DemoRocketPlanes r = build_demo_rocket();
    const Vec2d position{1.5, 0.0};
    const Vec2d velocity{0.0, std::sqrt(1.0 / 1.5)};  // circular at r = 1.5
    VehicleFlight flight = make_flight(r, config, position, velocity);
    flight.advance(10.0);
    const aegis::orbit::Elements elements =
        aegis::orbit::elements_from_state({position.x, position.y}, {velocity.x, velocity.y}, 1.0, 0.0);
    const aegis::orbit::State expected = aegis::orbit::state_at(elements, 10.0);
    AEGIS_CHECK(near(flight.state().position.x, expected.position[0], 1e-6));
    AEGIS_CHECK(near(flight.state().position.y, expected.position[1], 1e-6));
}

// ---------------------------------------------------------------------------
// RCS blocks + monoprop tank + landing legs + fairing ballistics (Milestone 10.3)
// ---------------------------------------------------------------------------

constexpr TemplateId kFairingShell = 10;
constexpr TemplateId kRcsBlock = 13;
constexpr TemplateId kMonopropTank = 14;
constexpr TemplateId kLandingLeg = 15;

// Minimal pitch-authority vehicle: pod + 2 RCS blocks on the laterals, with
// an optional monoprop tank slung underneath. Baseline control torque is
// zeroed so the RCS is the only torque source.
VehicleAssembly build_rcs_rocket(const bool with_monoprop) {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    EXPECT_PHYS_OK(pod);
    core::Result<PartIndex> left = asmbl.snap(pod.value(), 1, kRcsBlock, 3, 0);
    EXPECT_PHYS_OK(left);
    core::Result<PartIndex> right = asmbl.snap(pod.value(), 2, kRcsBlock, 2, 0);
    EXPECT_PHYS_OK(right);
    if (with_monoprop) {
        core::Result<PartIndex> tank = asmbl.snap(pod.value(), 0, kMonopropTank, 0, 0);
        EXPECT_PHYS_OK(tank);
    }
    return asmbl;
}

VehicleFlight make_rcs_flight(VehicleAssembly asmbl, const f64 control_torque) {
    VehicleConfig config = airless_config(0.0);
    config.rotation.control_torque = control_torque;
    StagingList staging;  // nothing staged
    core::Result<VehicleFlight> flight = VehicleFlight::create(std::move(asmbl), std::move(staging),
                                                               config, Vec2d{0.0, 100.0}, Vec2d{0.0, 0.0}, kHalfPi);
    EXPECT_PHYS_OK(flight);
    return std::move(flight.value());
}

AEGIS_TEST(physics, "RCS blocks add pitch torque only while monoprop remains") {
    {  // no monoprop -> zero authority
        VehicleFlight flight = make_rcs_flight(build_rcs_rocket(/*with_monoprop=*/false), 0.0);
        flight.set_pitch_input(1.0);
        for (int i = 0; i < 120; ++i) {
            flight.tick(VehicleFlight::kFixedDt);
        }
        AEGIS_CHECK(flight.state().angular_velocity == 0.0);
        AEGIS_CHECK(flight.state().heading == kHalfPi);
    }
    {  // monoprop -> positive pitch torque from rest
        VehicleFlight flight = make_rcs_flight(build_rcs_rocket(/*with_monoprop=*/true), 0.0);
        flight.set_pitch_input(1.0);
        for (int i = 0; i < 120; ++i) {
            flight.tick(VehicleFlight::kFixedDt);
        }
        AEGIS_CHECK(flight.state().angular_velocity > 1e-3);
        AEGIS_CHECK(flight.state().heading > kHalfPi);
    }
}

AEGIS_TEST(physics, "RCS monoprop drains proportionally to pitch deflection") {
    VehicleFlight flight = make_rcs_flight(build_rcs_rocket(/*with_monoprop=*/true), 0.0);
    flight.set_pitch_input(1.0);
    const int ticks = 120;  // 0.5 s at 240 Hz
    for (int i = 0; i < ticks; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    const f64 consumed = 140.0 - static_cast<f64>(flight.assembly().part(3)->propellant);
    const f64 expected = 2.0 * 0.9 * (static_cast<f64>(ticks) * VehicleFlight::kFixedDt);
    AEGIS_CHECK(near(consumed, expected, 5e-3));
    // Zero deflection must not drain anything.
    flight.set_pitch_input(0.0);
    const f32 before = flight.assembly().part(3)->propellant;
    for (int i = 0; i < 120; ++i) {
        flight.tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.assembly().part(3)->propellant == before);
}

AEGIS_TEST(physics, "main engines flame out instead of draining monoprop") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    EXPECT_PHYS_OK(pod);
    const core::Result<PartIndex> engine = asmbl.snap(pod.value(), 0, 4, 0, 0);  // sparrow under pod
    EXPECT_PHYS_OK(engine);
    const core::Result<PartIndex> tank = asmbl.snap(engine.value(), 1, kMonopropTank, 0, 0);
    EXPECT_PHYS_OK(tank);
    VehicleConfig config = airless_config(0.0);
    StagingList staging;
    EXPECT_PHYS_OK(staging.assign(asmbl, engine.value(), 0));
    core::Result<VehicleFlight> flight = VehicleFlight::create(std::move(asmbl), std::move(staging), config,
                                                               Vec2d{0.0, 100.0}, Vec2d{0.0, 0.0}, kHalfPi);
    EXPECT_PHYS_OK(flight);
    EXPECT_PHYS_OK(flight.value().activate_next_stage());
    flight.value().set_throttle(1.0);
    for (int i = 0; i < 30; ++i) {
        flight.value().tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.value().assembly().part(tank.value())->propellant == 140.0f);
    std::vector<FlightEvent> events;
    flight.value().take_events(events);
    AEGIS_CHECK_EQ(std::count(events.begin(), events.end(), FlightEvent::EngineFlameout), 1);
}

AEGIS_TEST(physics, "jettisoned fairing cluster separates as a ballistic free-flyer") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    EXPECT_PHYS_OK(pod);
    const core::Result<PartIndex> shell = asmbl.snap(pod.value(), 3, kFairingShell, 1, 0);  // shell on pod top
    EXPECT_PHYS_OK(shell);
    const core::Result<PartIndex> nose = asmbl.snap(shell.value(), 0, 11, 0, 0);  // nose on shell top
    EXPECT_PHYS_OK(nose);
    const f64 mass_before = static_cast<f64>(asmbl.mass_properties().total_mass);
    VehicleConfig config = airless_config(1.0);  // real gravity for the ballistic check
    StagingList staging;
    EXPECT_PHYS_OK(staging.assign(asmbl, shell.value(), 0));
    EXPECT_PHYS_OK(staging.assign(asmbl, nose.value(), 0));
    core::Result<VehicleFlight> flight = VehicleFlight::create(std::move(asmbl), std::move(staging), config,
                                                               Vec2d{0.0, 100.0}, Vec2d{30.0, 0.0}, kHalfPi);
    EXPECT_PHYS_OK(flight);
    EXPECT_PHYS_OK(flight.value().activate_next_stage());
    AEGIS_CHECK_EQ(flight.value().detached_stages().size(), 1u);
    AEGIS_CHECK_EQ(flight.value().detached_stages().front().assembly.alive_count(), 2u);
    AEGIS_CHECK_EQ(flight.value().assembly().alive_count(), 1u);  // pod alone
    AEGIS_CHECK(flight.value().assembly().part(shell.value()) == nullptr);
    AEGIS_CHECK(flight.value().assembly().part(nose.value()) == nullptr);
    const f64 mass_after = static_cast<f64>(flight.value().mass_properties().total_mass) +
                           static_cast<f64>(flight.value().detached_stages().front().assembly.mass_properties().total_mass);
    AEGIS_CHECK(near(mass_after, mass_before, 1e-3));
    // Zero relative separation velocity: the cluster rides the vehicle's frame.
    AEGIS_CHECK(flight.value().detached_stages().front().state.velocity.x == 30.0);
    // And it falls ballistically under real gravity.
    const Vec2d before = flight.value().detached_stages().front().state.position;
    for (int i = 0; i < 120; ++i) {
        flight.value().tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.value().detached_stages().front().state.position.y < before.y);
}

AEGIS_TEST(physics, "landing legs add mass and inertia but no control authority") {
    VehicleAssembly pod_only;
    const core::Result<PartIndex> pod = pod_only.add_root(kPod, 0, 0, 0);
    EXPECT_PHYS_OK(pod);
    const auto alone = pod_only.mass_properties();

    VehicleAssembly with_legs;
    const core::Result<PartIndex> root = with_legs.add_root(kPod, 0, 0, 0);
    EXPECT_PHYS_OK(root);
    core::Result<PartIndex> left = with_legs.snap(root.value(), 1, kLandingLeg, 3, 0);
    EXPECT_PHYS_OK(left);
    core::Result<PartIndex> right = with_legs.snap(root.value(), 2, kLandingLeg, 2, 0);
    EXPECT_PHYS_OK(right);
    const auto armed = with_legs.mass_properties();
    AEGIS_CHECK(near(static_cast<f64>(armed.total_mass), static_cast<f64>(alone.total_mass) + 180.0, 1e-3));
    AEGIS_CHECK(armed.inertia_izz > alone.inertia_izz);

    VehicleConfig config = airless_config(0.0);
    config.rotation.control_torque = 0.0;  // nothing but pseudo-forces can turn the rocket
    StagingList staging;
    core::Result<VehicleFlight> flight = VehicleFlight::create(std::move(with_legs), std::move(staging), config,
                                                               Vec2d{0.0, 100.0}, Vec2d{0.0, 0.0}, kHalfPi);
    EXPECT_PHYS_OK(flight);
    flight.value().set_pitch_input(1.0);
    for (int i = 0; i < 120; ++i) {
        flight.value().tick(VehicleFlight::kFixedDt);
    }
    AEGIS_CHECK(flight.value().state().angular_velocity == 0.0);
}
