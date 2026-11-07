// Aegis2D — orbital mechanics (two-body Keplerian propagation), Vulkan-free.
//
// 2D plane in "world units" (same unit convention as the renderer: y up).
// All angles are radians; time is in seconds; `mu` is the gravitational
// parameter in world_units^3 / s^2.
//
// Kernel dispatch (Milestone 6): when the engine is configured with a Fortran
// compiler, the numerical kernels (solve_kepler / state_at / sample_path)
// run as Fortran modules behind the C ABI in aegis/orbit/orbit_c.h ("aegis_
// solve_kepler_f64" et al.). Otherwise the C++ fallback kernels in
// detail::fallback_* (orbit_fallback.cpp) are used — numerically identical.
#include <aegis/orbit/orbit.hpp>
#include <aegis/orbit/internal.hpp>

#ifdef AEGIS_HAVE_FORTRAN
#include <aegis/orbit/orbit_c.h>
#endif

#include <cmath>
#include <vector>

namespace aegis::orbit {

using core::f64;

namespace {

constexpr f64 kTwoPi = 6.2831853071795864769;

}  // namespace

f64 orbital_period(const Elements& elements) noexcept {
    const f64 a = elements.semi_major_axis;
    return kTwoPi * std::sqrt(a * a * a / elements.gravitational_parameter);
}

f64 mean_motion(const Elements& elements) noexcept {
    const f64 a = elements.semi_major_axis;
    return std::sqrt(elements.gravitational_parameter / (a * a * a));
}

f64 solve_kepler(const f64 mean_anomaly, const f64 eccentricity) noexcept {
#ifdef AEGIS_HAVE_FORTRAN
    return aegis_solve_kepler_f64(mean_anomaly, eccentricity);
#else
    return detail::fallback_solve_kepler(mean_anomaly, eccentricity);
#endif
}

State state_at(const Elements& elements, const f64 t) noexcept {
#ifdef AEGIS_HAVE_FORTRAN
    double out[8] = {};
    aegis_state_at_f64(elements.semi_major_axis, elements.eccentricity,
                       elements.periapsis_angle, elements.gravitational_parameter,
                       elements.periapsis_time, t, out);
    State state{};
    state.position = {out[0], out[1]};
    state.velocity = {out[2], out[3]};
    state.radius = out[4];
    state.speed = std::hypot(out[2], out[3]);  // same single-call libm path as the fallback
    state.true_anomaly = out[5];
    state.mean_anomaly = out[6];
    state.eccentric_anomaly = out[7];
    return state;
#else
    return detail::fallback_state_at(elements, t);
#endif
}

std::vector<std::array<core::f32, 2>> sample_path(const Elements& elements, const core::u32 samples) {
    std::vector<std::array<core::f32, 2>> points;
    if (samples < 3) {
        return points;
    }
#ifdef AEGIS_HAVE_FORTRAN
    points.resize(samples);
    static_assert(sizeof(std::array<core::f32, 2>) == 2 * sizeof(core::f32),
                  "std::array<float, 2> must be layout-compatible with float[2]");
    auto* const flat_xy = reinterpret_cast<core::f32*>(points.data());
    aegis_sample_path_f32(elements.semi_major_axis, elements.eccentricity,
                          elements.periapsis_angle, static_cast<int>(samples), flat_xy);
#else
    points = detail::fallback_sample_path(elements, samples);
#endif
    return points;
}

}  // namespace aegis::orbit
