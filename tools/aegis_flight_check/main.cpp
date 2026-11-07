// Aegis2D — aegis_flight_check: headless Milestone-8 verification tool.
//
// Flies the deterministic three-stage rocket (built via the public M7 API)
// from the surface on an Earth-like body: liftoff on three Sparrows, stage
// separation with momentum-conserving delta-v, upper-stage burn, jettison,
// ballistic apogee. Asserts the machinery end to end; prints the trajectory
// trace; exits 0 on success with FLIGHT/ASCENT_VERIFIED, 1 on any failed
// check. Driver-free, deterministic (fixed 1/240 s integration step).
#include <aegis/core/log.hpp>
#include <aegis/physics/vehicle_flight.hpp>
#include <aegis/parts/staging.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace aegis;
using namespace aegis::physics;
using namespace aegis::parts;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                              \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::fprintf(stderr, "FAIL [%d] %s\n", __LINE__, (what));                   \
        }                                                                              \
    } while (0)

constexpr TemplateId kPod = 0;
constexpr TemplateId kShortTank = 3;
constexpr TemplateId kSparrow = 4;
constexpr TemplateId kKestrel = 6;
constexpr TemplateId kDecoupler = 7;
constexpr TemplateId kRadialSeparator = 9;
constexpr TemplateId kNoseFairing = 11;

static constexpr char kEventName[][24] = {"STAGE_SEPARATED", "FAIRING_JETTISONED", "ENGINE_IGNITED",
                                          "ENGINE_FLAMEOUT", "TOUCHDOWN",         "IMPACT"};

int main() {
    // ---------------------------------------------------------------
    // Build the rocket: Sparrow liftoff stage + two radial boosters feed a
    // Kestrel upper stage with four loaded Short-1 tanks (1680 kg total).
    // ---------------------------------------------------------------
    VehicleAssembly asmbl;
    auto add = [&asmbl](const TemplateId tmpl, const i16 gx, const i16 gy, const u8 rot) {
        core::Result<PartIndex> r = asmbl.add_root(tmpl, gx, gy, rot);
        if (!r.has_value()) {
            std::fprintf(stderr, "add_root failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
        return r.value();
    };
    auto snap = [&asmbl](const PartIndex parent, const NodeId pnode, const TemplateId child,
                         const NodeId cnode, const u8 rot) {
        core::Result<PartIndex> r = asmbl.snap(parent, pnode, child, cnode, rot);
        if (!r.has_value()) {
            std::fprintf(stderr, "snap failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
        return r.value();
    };

    const PartIndex liftoff = add(kSparrow, 0, 0, 0);                      // 0
    const PartIndex sep1 = snap(liftoff, 0, kDecoupler, 1, 0);             // 1
    const PartIndex kestrel = snap(sep1, 0, kKestrel, 1, 0);               // 2
    const PartIndex tank2 = snap(kestrel, 0, kShortTank, 1, 0);            // 3
    const PartIndex sep2 = snap(tank2, 0, kDecoupler, 1, 0);               // 4
    const PartIndex tank3 = snap(sep2, 0, kShortTank, 1, 0);               // 5
    const PartIndex pod = snap(tank3, 0, kPod, 0, 0);                      // 6
    const PartIndex fairing = snap(pod, 3, kNoseFairing, 0, 0);            // 7

    const PartIndex sep_left = snap(tank2, 2, kRadialSeparator, 1, 0);     // 8
    const PartIndex booster_left_tank = snap(sep_left, 0, kShortTank, 3, 0);  // 9
    const PartIndex booster_left_engine = snap(booster_left_tank, 1, kSparrow, 0, 0);  // 10
    const core::Result<PartIndex> sep_right_res =
        asmbl.mirror_snap(tank2, 2, kRadialSeparator, 1, 0, Axis::Vertical, 0.5f);
    if (!sep_right_res.has_value()) {
        std::fprintf(stderr, "mirror_snap failed: %s\n", sep_right_res.error().message.c_str());
        return 1;
    }
    const PartIndex sep_right = sep_right_res.value();                     // 11
    const PartIndex booster_right_tank = snap(sep_right, 0, kShortTank, 2, 0);  // 12
    const PartIndex booster_right_engine = snap(booster_right_tank, 1, kSparrow, 0, 0);  // 13

    // ---------------------------------------------------------------
    // Flight plan (manual): stage 0 liftoff (three engines), stage 1 the
    // whole liftoff group separates at once, stage 2 Kestrel, stage 3 the
    // stage-2 separator, stage 4 the fairing.
    // ---------------------------------------------------------------
    StagingList staging;
    auto assign = [&staging, &asmbl](const PartIndex part, const u8 stage) {
        core::Result<void> r = staging.assign(asmbl, part, stage);
        if (!r.has_value()) {
            std::fprintf(stderr, "assign failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
    };
    assign(liftoff, 0);
    assign(booster_left_engine, 0);
    assign(booster_right_engine, 0);
    assign(sep1, 1);
    assign(sep_left, 1);
    assign(sep_right, 1);
    assign(kestrel, 2);
    assign(sep2, 3);
    assign(fairing, 4);

    VehicleConfig config;  // Earth-like body (SI defaults)
    config.aero.drag_coefficient = 0.35;
    config.aero.reference_area = 1.0;
    config.rotation.control_torque = 0.0;
    config.rotation.angular_damping = 0.05;

    core::Result<VehicleFlight> flight_res = VehicleFlight::create(
        std::move(asmbl), std::move(staging), config,
        Vec2d{0.0, config.body.radius}, Vec2d{}, 1.5707963267948966);
    if (!flight_res.has_value()) {
        std::fprintf(stderr, "create failed: %s\n", flight_res.error().message.c_str());
        return 1;
    }
    VehicleFlight flight = std::move(flight_res.value());
    flight.set_throttle(1.0);

    // Scripted stage activations (fixed times; deterministic). Stage 0 fires
    // the liftoff ignitions at t=0; the whole liftoff group separates at t=15;
    // Kestrel ignites at t=16; stage-2 separator at t=40; fairing at t=41.
    const std::vector<std::pair<f64, const char*>> schedule{
        {0.0, "liftoff ignition"},   {15.0, "liftoff group separation"}, {16.0, "Kestrel ignition"},
        {40.0, "stage-2 separation"}, {41.0, "fairing jettison"}};
    std::size_t next_activation = 0;

    const f64 dt = VehicleFlight::kFixedDt;
    f64 apogee = 0.0;
    u32 ignitions = 0, separations = 0, fairing_jettisons = 0, flameouts = 0, impacts = 0, touchdowns = 0;
    std::vector<FlightEvent> pending;

    const f64 duration = 150.0;
    f64 next_report = 0.0;
    for (f64 t = 0.0; t < duration; t += dt) {
        while (next_activation < schedule.size() && t >= schedule[next_activation].first) {
            core::Result<void> r = flight.activate_next_stage();
            CHECK(r.has_value(), schedule[next_activation].second);
            ++next_activation;
        }

        flight.tick(dt);

        flight.take_events(pending);
        for (const FlightEvent ev : pending) {
            const u8 idx = static_cast<u8>(ev);
            std::printf("  [t=%6.2f] event: %s\n", t, kEventName[idx]);
            switch (ev) {
                case FlightEvent::EngineIgnited: ++ignitions; break;
                case FlightEvent::StageSeparated: ++separations; break;
                case FlightEvent::FairingJettisoned: ++fairing_jettisons; break;
                case FlightEvent::EngineFlameout: ++flameouts; break;
                case FlightEvent::Impact: ++impacts; break;
                case FlightEvent::Touchdown: ++touchdowns; break;
            }
        }

        const FlightState& state = flight.state();
        CHECK(std::isfinite(state.position.x) && std::isfinite(state.position.y), "position finite");
        CHECK(std::isfinite(state.velocity.x) && std::isfinite(state.velocity.y), "velocity finite");
        const f64 altitude = state.position.length() - config.body.radius;
        apogee = std::max(apogee, altitude);

        if (t >= next_report) {
            const parts::MassProperties& mp = flight.mass_properties();
            std::printf("  t=%6.1f  alt=%9.1f m  v=%8.1f m/s  fuel=%7.1f kg  CoM=(%.2f, %.2f) w_u  total=%.1f kg\n",
                        t, altitude, state.velocity.length(),
                        static_cast<double>(mp.propellant_mass),
                        static_cast<double>(mp.center_of_mass[0]),
                        static_cast<double>(mp.center_of_mass[1]),
                        static_cast<double>(mp.total_mass));
            next_report += 5.0;
        }
        if (flight.finished()) {
            std::printf("  flight terminated at t=%.2f s\n", t);
            break;
        }
    }

    // ---------------------------------------------------------------
    // Final invariants.
    // ---------------------------------------------------------------
    std::printf("apogee=%.1f m  ignitions=%u separations=%u fairings=%u flameouts=%u touchdowns=%u impacts=%u\n",
                apogee, ignitions, separations, fairing_jettisons, flameouts, touchdowns, impacts);

    CHECK(apogee >= 5000.0, "apogee above 5 km");
    CHECK(ignitions == 4, "liftoff trio plus Kestrel ignited (4 total)");
    CHECK(separations == 4, "four separators fired (3 liftoff group + sep2)");
    CHECK(fairing_jettisons == 1, "fairing jettisoned");
    CHECK(flight.detached_stages().size() == 5, "four separators plus the jettisoned fairing tracked");
    CHECK(flight.assembly().alive_count() == 2, "final vehicle is pod + tank3");

    const parts::MassProperties& mp = flight.mass_properties();
    CHECK(mp.total_mass >= 800.0f + 160.0f, "final mass covers pod + tank dry mass");
    CHECK(mp.total_mass <= 800.0f + 160.0f + 420.0f, "final mass within full-tank bound");

    // Propellant conservation across every component (main vehicle + debris).
    f64 remaining = static_cast<f64>(mp.propellant_mass);
    for (const DetachedStage& stage : flight.detached_stages()) {
        remaining += static_cast<f64>(stage.assembly.mass_properties().propellant_mass);
        for (std::size_t i = 0; i < stage.assembly.parts().size(); ++i) {
            const parts::Part* p = stage.assembly.part(static_cast<PartIndex>(i));
            if (p) {
                CHECK(p->propellant >= 0.0f, "no negative propellant in debris");
            }
        }
    }
    const f64 drained = 1680.0 - remaining;
    std::printf("propellant drained=%.1f kg of 1680.0 kg (remaining total=%.1f kg)\n", drained, remaining);
    CHECK(drained > 500.0, "substantial burn occurred");
    CHECK(remaining >= 0.0, "propellant conservation holds (sum non-negative)");

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("FLIGHT/ASCENT_FAILED\n");
        return 1;
    }
    std::printf("FLIGHT/ASCENT_VERIFIED\n");
    return 0;
}
