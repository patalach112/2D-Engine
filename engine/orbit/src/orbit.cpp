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
constexpr f64 kPi = 3.14159265358979323846;

}  // namespace

f64 orbital_period(const Elements& elements) noexcept {
    const f64 a = elements.semi_major_axis;
    return kTwoPi * std::sqrt(a * a * a / elements.gravitational_parameter);
}

f64 mean_motion(const Elements& elements) noexcept {
    const f64 a = elements.semi_major_axis;
    return std::sqrt(elements.gravitational_parameter / (a * a * a));
}

Elements elements_from_state(std::array<core::f64, 2> position,
                             std::array<core::f64, 2> velocity,
                             const f64 gravitational_parameter, const f64 t) noexcept {
    const f64 mu = gravitational_parameter;
    const f64 r = std::hypot(position[0], position[1]);
    const f64 v2 = velocity[0] * velocity[0] + velocity[1] * velocity[1];
    const f64 r_dot_v = position[0] * velocity[0] + position[1] * velocity[1];

    // Eccentricity vector: e_vec = ((v^2 - mu/r) * r_vec - (r·v) * v_vec) / mu
    const f64 scale = (v2 - mu / r) / mu;
    const f64 evx = scale * position[0] - (r_dot_v / mu) * velocity[0];
    const f64 evy = scale * position[1] - (r_dot_v / mu) * velocity[1];
    const f64 eccentricity = std::hypot(evx, evy);

    // Semi-major axis from orbital energy (negative when unbound).
    const f64 energy = v2 * 0.5 - mu / r;
    const f64 semi_major_axis = -mu / (2.0 * energy);

    Elements elements{};
    elements.eccentricity = eccentricity;
    elements.semi_major_axis = semi_major_axis;
    elements.gravitational_parameter = mu;

    const f64 r_vec_angle = std::atan2(position[1], position[0]);
    if (eccentricity > 1e-12) {
        elements.periapsis_angle = std::atan2(evy, evx);
    } else {
        elements.periapsis_angle = 0.0;  // circular — periapsis direction is arbitrary
    }

    // True anomaly: angle from periapsis direction to the radius vector.
    f64 true_anomaly = r_vec_angle - elements.periapsis_angle;
    while (true_anomaly > kPi) {
        true_anomaly -= kTwoPi;
    }
    while (true_anomaly <= -kPi) {
        true_anomaly += kTwoPi;
    }

    if (!(semi_major_axis > 0.0) || eccentricity >= 1.0) {
        elements.periapsis_time = t;  // unbound trajectory — state_at() precondition violated anyway
        return elements;
    }

    // Eccentric anomaly via the half-angle form (quadrant-safe).
    const f64 half = true_anomaly * 0.5;
    const f64 eccentric_anomaly = 2.0 * std::atan2(std::sqrt(1.0 - eccentricity) * std::sin(half),
                                                   std::sqrt(1.0 + eccentricity) * std::cos(half));
    const f64 mean_anomaly = eccentric_anomaly - eccentricity * std::sin(eccentric_anomaly);
    const f64 n = std::sqrt(mu / (semi_major_axis * semi_major_axis * semi_major_axis));
    elements.periapsis_time = t - mean_anomaly / n;
    return elements;
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
