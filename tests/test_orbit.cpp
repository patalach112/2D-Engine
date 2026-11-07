// Aegis2D — unit tests for Keplerian orbital mechanics (Milestone 4).
#include <aegis/orbit/orbit.hpp>

#include "aegis_test.hpp"

#include <array>
#include <cmath>
#include <string>

using aegis::core::f64;
using aegis::orbit::Elements;
using aegis::orbit::State;

namespace {

constexpr f64 kTwoPi = 6.2831853071795864769;
constexpr f64 kPi = 3.1415926535897932384;

void expect_near(const f64 actual, const f64 expected, const f64 eps, const char* expr) {
    ++::aegis::test::check_count();
    const f64 delta = std::fabs(actual - expected);
    if (delta > eps) {
        ::aegis::test::report_failure(__FILE__, __LINE__,
                                      std::string("expect_near failed: ") + expr +
                                          " (got " + std::to_string(actual) + ", expected " +
                                          std::to_string(expected) + ", delta " + std::to_string(delta) + ")");
    }
}

Elements demo_elements(f64 eccentricity) {
    Elements elements;
    elements.semi_major_axis = 430.0;
    elements.eccentricity = eccentricity;
    elements.periapsis_angle = 0.4363;  // ~25°
    elements.gravitational_parameter = 39.4784176043574 * 430.0 * 430.0 * 430.0 / (26.0 * 26.0);
    elements.periapsis_time = 0.0;
    return elements;
}

f64 radius_of(const State& state) {
    return std::hypot(state.position[0], state.position[1]);
}

}  // namespace

AEGIS_TEST(orbit, "kepler solver: circular orbit is the identity map E = M") {
    for (const f64 M : {0.0, 0.5, 1.9, 3.3, 5.1, 6.0}) {
        expect_near(aegis::orbit::solve_kepler(M, 0.0), M, 1e-12, "e = 0 -> E == M");
    }
}

AEGIS_TEST(orbit, "kepler solver inverts M = E - e sin(E) across eccentricities") {
    for (const f64 e : {0.1, 0.28, 0.5, 0.75, 0.9}) {
        for (const f64 M : {0.0, 0.3, 1.2, 2.7, 3.6, 4.9, 6.1}) {
            const f64 E = aegis::orbit::solve_kepler(M, e);
            f64 residual = E - e * std::sin(E) - M;
            // Normalize the residual angle to [-pi, pi] for the assertion.
            residual = std::fmod(residual + kPi, kTwoPi);
            if (residual < 0.0) {
                residual += kTwoPi;
            }
            residual -= kPi;
            expect_near(std::fabs(residual), 0.0, 1e-11, "E - e sin(E) == M");
        }
    }
}

AEGIS_TEST(orbit, "kepler solver: normalization maps out-of-range M into [0, 2*pi)") {
    const f64 E1 = aegis::orbit::solve_kepler(0.3, 0.5);
    const f64 E2 = aegis::orbit::solve_kepler(0.3 + 4.0 * kTwoPi, 0.5);
    const f64 E3 = aegis::orbit::solve_kepler(0.3 - 3.0 * kTwoPi, 0.5);
    expect_near(E2, E1, 1e-9, "M + 4*2pi solves identically");
    expect_near(E3, E1, 1e-9, "M - 3*2pi solves identically");
}

AEGIS_TEST(orbit, "circular orbit: radius and speed stay constant") {
    const Elements elements = demo_elements(0.0);
    const f64 expected_speed = std::sqrt(elements.gravitational_parameter / elements.semi_major_axis);  // sqrt(mu/a)
    for (const f64 t : {0.0, 3.7, 9.1, 13.0, 22.5, 25.9}) {
        const State state = aegis::orbit::state_at(elements, t);
        expect_near(radius_of(state), elements.semi_major_axis, 1e-6, "circular radius == a");
        expect_near(state.speed, expected_speed, 1e-6, "circular speed == sqrt(mu/a)");
    }
}

AEGIS_TEST(orbit, "at periapsis passage the radius is exactly a(1 - e)") {
    const Elements elements = demo_elements(0.28);
    const State state = aegis::orbit::state_at(elements, elements.periapsis_time);
    expect_near(state.radius, aegis::orbit::periapsis_radius(elements), 1e-6,
                "periapsis radius == a(1-e)");
    // Position is aligned with periapsis direction (angle omega).
    const f64 angle = std::atan2(state.position[1], state.position[0]);
    expect_near(std::sin(angle - elements.periapsis_angle), 0.0, 1e-9,
                "periapsis direction == omega");
}

AEGIS_TEST(orbit, "half a period later the radius is exactly a(1 + e)") {
    const Elements elements = demo_elements(0.28);
    const f64 half_period = aegis::orbit::orbital_period(elements) * 0.5;
    const State state = aegis::orbit::state_at(elements, elements.periapsis_time + half_period);
    expect_near(state.radius, aegis::orbit::apoapsis_radius(elements), 1e-6,
                "apoapsis radius == a(1+e)");
}

AEGIS_TEST(orbit, "specific orbital energy is conserved along the trajectory") {
    const Elements elements = demo_elements(0.28);
    const f64 mu = elements.gravitational_parameter;
    const f64 a = elements.semi_major_axis;
    const f64 expected_energy = -mu / (2.0 * a);
    for (const f64 t : {0.0, 2.5, 6.25, 11.75, 18.0, 24.0}) {
        const State state = aegis::orbit::state_at(elements, t);
        const f64 energy = 0.5 * state.speed * state.speed - mu / state.radius;
        expect_near(energy, expected_energy, 1e-6, "epsilon == -mu / (2a)");
    }
}

AEGIS_TEST(orbit, "speed peaks at periapsis and bottoms out at apoapsis") {
    const Elements elements = demo_elements(0.28);
    const f64 v_peri = std::sqrt(elements.gravitational_parameter *
                                 (2.0 / aegis::orbit::periapsis_radius(elements) -
                                  1.0 / elements.semi_major_axis));
    const f64 v_apo = std::sqrt(elements.gravitational_parameter *
                                (2.0 / aegis::orbit::apoapsis_radius(elements) -
                                 1.0 / elements.semi_major_axis));
    const State peri = aegis::orbit::state_at(elements, 0.0);
    const f64 half_period = aegis::orbit::orbital_period(elements) * 0.5;
    const State apo = aegis::orbit::state_at(elements, half_period);
    expect_near(peri.speed, v_peri, 1e-6, "vis-viva speed at periapsis");
    expect_near(apo.speed, v_apo, 1e-6, "vis-viva speed at apoapsis");
    AEGIS_CHECK(peri.speed > apo.speed);
}

AEGIS_TEST(orbit, "sampled path stays on the ellipse and starts at periapsis") {
    const Elements elements = demo_elements(0.28);
    const auto points = aegis::orbit::sample_path(elements, 128);
    AEGIS_REQUIRE(points.size() == std::size_t{128});

    const f64 peri = aegis::orbit::periapsis_radius(elements);
    const f64 apo = aegis::orbit::apoapsis_radius(elements);
    for (const auto& [x, y] : points) {
        const f64 r = std::hypot(static_cast<f64>(x), static_cast<f64>(y));
        AEGIS_CHECK(r >= peri * (1.0 - 1e-5));
        AEGIS_CHECK(r <= apo * (1.0 + 1e-5));
    }
    expect_near(std::hypot(static_cast<f64>(points[0][0]), static_cast<f64>(points[0][1])), peri,
                1e-4, "first sample (E = 0) is at periapsis");
}

AEGIS_TEST(orbit, "propagation stays bounded far into the future") {
    const Elements elements = demo_elements(0.28);
    const f64 peri = aegis::orbit::periapsis_radius(elements);
    const f64 apo = aegis::orbit::apoapsis_radius(elements);
    const State state = aegis::orbit::state_at(elements, 1.0e6);  // ~10.5 hours
    AEGIS_CHECK(state.radius >= peri * (1.0 - 1e-6));
    AEGIS_CHECK(state.radius <= apo * (1.0 + 1e-6));
    AEGIS_CHECK(state.mean_anomaly >= 0.0 && state.mean_anomaly < kTwoPi);
}

AEGIS_TEST(orbit, "period and radius helpers agree with the elements") {
    const Elements elements = demo_elements(0.3);
    expect_near(aegis::orbit::periapsis_radius(elements), 301.0, 1e-9, "a(1-e)");
    expect_near(aegis::orbit::apoapsis_radius(elements), 559.0, 1e-9, "a(1+e)");
    expect_near(aegis::orbit::orbital_period(elements), 26.0, 1e-6, "period == demo T");
    expect_near(aegis::orbit::mean_motion(elements), kTwoPi / 26.0, 1e-9, "n == 2pi / T");
}
