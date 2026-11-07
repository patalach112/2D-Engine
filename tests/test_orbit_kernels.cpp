// Aegis2D — A/B equivalence tests: Fortran kernels vs the C++ fallback.
//
// When the engine is configured without a Fortran compiler (AEGIS_HAVE_FORTRAN
// undefined), the Fortran half of these checks is replaced by a documented
// no-op availability test, mirroring the driverless-skip pattern used by
// aegis_roundtrip / aegis_rocket_dump.
#include <aegis/orbit/orbit.hpp>
#include <aegis/orbit/internal.hpp>
// Included unconditionally: validates the pure-C declarations parse as C++ too
// (the symbols themselves only exist when the Fortran kernels are enabled).
#include <aegis/orbit/orbit_c.h>

#include "aegis_test.hpp"

#include <cmath>
#include <vector>

namespace {

using aegis::core::f32;
using aegis::core::f64;
using aegis::orbit::Elements;
using aegis::orbit::State;

constexpr f64 kTwoPi = 6.2831853071795864769;

[[maybe_unused]] Elements demo_elements() {
    Elements elements;
    elements.semi_major_axis = 430.0;
    elements.eccentricity = 0.28;
    elements.periapsis_angle = 0.4363;  // ~25 deg
    elements.gravitational_parameter =
        std::pow(kTwoPi / 26.0, 2.0) * 430.0 * 430.0 * 430.0;  // T = 26 s demo orbit
    elements.periapsis_time = 0.0;
    return elements;
}

[[maybe_unused]] bool near(const f64 a, const f64 b, const f64 tolerance) {
    return std::fabs(a - b) <= tolerance;
}

}  // namespace

#ifdef AEGIS_HAVE_FORTRAN

AEGIS_TEST(orbit_kernels, "Fortran and C++ Kepler solvers agree on an anomaly grid") {
    for (const f64 eccentricity : {0.0, 0.28, 0.5, 0.9}) {
        for (int k = 0; k < 997; ++k) {
            const f64 mean_anomaly = kTwoPi * static_cast<f64>(k) / 997.0;
            const f64 fortran_e = aegis::orbit::solve_kepler(mean_anomaly, eccentricity);
            const f64 fallback_e = aegis::orbit::detail::fallback_solve_kepler(mean_anomaly, eccentricity);
            AEGIS_CHECK(near(fortran_e, fallback_e, 1e-11));
        }
    }
    // Angle-range normalization (including negative and huge inputs).
    for (const f64 mean_anomaly : {1.0e8 + 0.3, -100.7, 12345.678}) {
        const f64 fortran_e = aegis::orbit::solve_kepler(mean_anomaly, 0.28);
        const f64 fallback_e = aegis::orbit::detail::fallback_solve_kepler(mean_anomaly, 0.28);
        AEGIS_CHECK(near(fortran_e, fallback_e, 1e-11));
    }
}

AEGIS_TEST(orbit_kernels, "Fortran and C++ state_at agree across the orbit") {
    const Elements elements = demo_elements();
    const f64 period = aegis::orbit::orbital_period(elements);
    const std::vector<f64> times = {0.0, 1.3, 6.5, 7.0, 13.0, 25.999, -123.4, 1.0e6};

    for (const f64 t : times) {
        const State fortran_state = aegis::orbit::state_at(elements, t);
        const State fallback_state = aegis::orbit::detail::fallback_state_at(elements, t);

        AEGIS_CHECK(near(fortran_state.position[0], fallback_state.position[0], 1e-7));
        AEGIS_CHECK(near(fortran_state.position[1], fallback_state.position[1], 1e-7));
        AEGIS_CHECK(near(fortran_state.velocity[0], fallback_state.velocity[0], 1e-9));
        AEGIS_CHECK(near(fortran_state.velocity[1], fallback_state.velocity[1], 1e-9));
        AEGIS_CHECK(near(fortran_state.radius, fallback_state.radius, 1e-7));
        AEGIS_CHECK(near(fortran_state.speed, fallback_state.speed, 1e-9));
        AEGIS_CHECK(near(fortran_state.true_anomaly, fallback_state.true_anomaly, 1e-10));
        AEGIS_CHECK(near(fortran_state.mean_anomaly, fallback_state.mean_anomaly, 1e-10));
        AEGIS_CHECK(near(fortran_state.eccentric_anomaly, fallback_state.eccentric_anomaly, 1e-10));
    }
    (void)period;
}

AEGIS_TEST(orbit_kernels, "Fortran and C++ sample_path agree") {
    const Elements elements = demo_elements();
    const auto fortran_path = aegis::orbit::sample_path(elements, 128);
    const auto fallback_path = aegis::orbit::detail::fallback_sample_path(elements, 128);

    AEGIS_REQUIRE(fortran_path.size() == fallback_path.size());
    AEGIS_REQUIRE(fortran_path.size() == 128u);
    for (std::size_t i = 0; i < fortran_path.size(); ++i) {
        AEGIS_CHECK(near(static_cast<f64>(fortran_path[i][0]),
                         static_cast<f64>(fallback_path[i][0]), 1e-3));
        AEGIS_CHECK(near(static_cast<f64>(fortran_path[i][1]),
                         static_cast<f64>(fallback_path[i][1]), 1e-3));
    }
}

#else  // AEGIS_HAVE_FORTRAN undefined

AEGIS_TEST(orbit_kernels, "Fortran backend disabled at configure time (C++ fallback active)") {
    // Documents the configuration; the equivalence checks above only make
    // sense with a Fortran compiler (AEGIS_ENABLE_FORTRAN=ON).
    AEGIS_CHECK(true);
}

#endif  // AEGIS_HAVE_FORTRAN
