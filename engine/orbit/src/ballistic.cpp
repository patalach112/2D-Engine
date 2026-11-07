// Aegis2D — ballistic (suborbital, gravity-only) trajectory prediction.
//
// Used by the orbital map to draw suborbital arcs that a Keplerian conic
// cannot represent, and to show an impact marker with a live countdown.
// Deterministic: fixed step count, no data-dependent branches in the
// integrator; the only adaptive part is the bracketed bisection that refines
// the surface crossing (a bounded, fixed-iteration loop).
#include <aegis/orbit/orbit.hpp>

#include <cmath>
#include <vector>

namespace aegis::orbit {

namespace {

using core::f64;

constexpr int kSteps = 1200;        ///< default integration steps over max_t
constexpr int kShortHopSteps = 300; ///< resolution for short suborbital hops (est <= 300 s)
constexpr int kMaxIterations = 4096;///< hard cap — bounds every path through the integrator
constexpr int kArcEvery = 3;        ///< push an arc point every N steps on the default path
constexpr int kBisectionIters = 48; ///< surface-crossing refinement (fixed)

struct Vec2State {
    std::array<f64, 2> p{};
    std::array<f64, 2> v{};
};

inline std::array<f64, 2> accel(const std::array<f64, 2>& p, const f64 mu) noexcept {
    const f64 r2 = p[0] * p[0] + p[1] * p[1];
    const f64 inv_r = 1.0 / std::sqrt(r2);
    const f64 inv_r3 = inv_r * inv_r * inv_r;
    return {-mu * p[0] * inv_r3, -mu * p[1] * inv_r3};
}

inline void rk4_step(Vec2State& s, const f64 h, const f64 mu) noexcept {
    const std::array<f64, 2> kp1 = s.v;
    const std::array<f64, 2> kv1 = accel(s.p, mu);

    const std::array<f64, 2> p2{s.p[0] + kp1[0] * (h * 0.5), s.p[1] + kp1[1] * (h * 0.5)};
    const std::array<f64, 2> v2{s.v[0] + kv1[0] * (h * 0.5), s.v[1] + kv1[1] * (h * 0.5)};
    const std::array<f64, 2> kp2 = v2;
    const std::array<f64, 2> kv2 = accel(p2, mu);

    const std::array<f64, 2> p3{s.p[0] + kp2[0] * (h * 0.5), s.p[1] + kp2[1] * (h * 0.5)};
    const std::array<f64, 2> v3{s.v[0] + kv2[0] * (h * 0.5), s.v[1] + kv2[1] * (h * 0.5)};
    const std::array<f64, 2> kp3 = v3;
    const std::array<f64, 2> kv3 = accel(p3, mu);

    const std::array<f64, 2> p4{s.p[0] + kp3[0] * h, s.p[1] + kp3[1] * h};
    const std::array<f64, 2> v4{s.v[0] + kv3[0] * h, s.v[1] + kv3[1] * h};
    const std::array<f64, 2> kp4 = v4;
    const std::array<f64, 2> kv4 = accel(p4, mu);

    s.p[0] += h * (kp1[0] + 2.0 * kp2[0] + 2.0 * kp3[0] + kp4[0]) / 6.0;
    s.p[1] += h * (kp1[1] + 2.0 * kp2[1] + 2.0 * kp3[1] + kp4[1]) / 6.0;
    s.v[0] += h * (kv1[0] + 2.0 * kv2[0] + 2.0 * kv3[0] + kv4[0]) / 6.0;
    s.v[1] += h * (kv1[1] + 2.0 * kv2[1] + 2.0 * kv3[1] + kv4[1]) / 6.0;
}

}  // namespace

BallisticPrediction predict_ballistic(std::array<core::f64, 2> position,
                                      std::array<core::f64, 2> velocity,
                                      const core::f64 gravitational_parameter,
                                      const core::f64 radius,
                                      const core::f64 max_t) {
    BallisticPrediction result;
    const auto altitude = [radius](const std::array<f64, 2>& p) noexcept {
        return std::hypot(p[0], p[1]) - radius;
    };
    const auto speed = [](const std::array<f64, 2>& v) noexcept {
        return std::hypot(v[0], v[1]);
    };

    const f64 alt0 = altitude(position);
    const f64 r0 = std::hypot(position[0], position[1]);
    const f64 vr0 = (position[0] * velocity[0] + position[1] * velocity[1]) / r0;
    if (alt0 < 0.0 || (alt0 <= 0.0 && vr0 <= 0.0)) {
        // Below the surface, or sitting on it without climbing away — the
        // impact already happened (or is happening right now).
        result.impacts = true;
        result.impact_in_s = 0.0;
        result.impact_position = position;
        result.impact_speed = speed(velocity);
        result.arc.push_back(position);
        return result;
    }
    // alt0 == 0 with vr0 > 0 (launching from the pad) is a legal start state:
    // integrate normally; the arc returns to the surface later.
    if (!std::isfinite(gravitational_parameter) || gravitational_parameter <= 0.0 ||
        !std::isfinite(radius) || radius <= 0.0 || !std::isfinite(max_t) || max_t <= 0.0) {
        return result;  // degenerate inputs — report "no prediction"
    }

    // Deterministic resolution: short suborbital hops get a fine step (arc is
    // smooth and impact refinement lands between nearby states); anything
    // else uses a fixed step over max_t. The iteration count is hard-capped.
    const f64 local_g = gravitational_parameter / (r0 * r0);
    const f64 est = (vr0 + std::sqrt(vr0 * vr0 + 2.0 * local_g * alt0)) / local_g;  // s (constant-g estimate)
    const bool short_hop = est > 0.0 && est <= 300.0;
    const f64 dt = short_hop ? est / static_cast<f64>(kShortHopSteps)
                             : max_t / static_cast<f64>(kSteps);
    const int arc_every = short_hop ? 1 : kArcEvery;

    Vec2State state{position, velocity};
    result.arc.reserve(static_cast<std::size_t>(kMaxIterations / arc_every) + 2u);
    result.arc.push_back(state.p);

    f64 t = 0.0;
    for (int step = 0; step < kMaxIterations && t < max_t; ++step) {
        const Vec2State prev = state;
        const f64 t_prev = t;
        rk4_step(state, dt, gravitational_parameter);
        t += dt;
        const f64 altitude_prev = altitude(prev.p);
        const f64 altitude_now = altitude(state.p);
        if (altitude_now <= 0.0 && altitude_prev >= 0.0) {
            // Bracketed bisection on the partial step: solve altitude(τ) = 0
            // with the same RK4 rule at a fraction τ of the step.
            f64 lo = 0.0, hi = dt;
            for (int iter = 0; iter < kBisectionIters; ++iter) {
                const f64 mid = 0.5 * (lo + hi);
                Vec2State trial{prev.p, prev.v};
                rk4_step(trial, mid, gravitational_parameter);
                if (altitude(trial.p) > 0.0) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            const f64 tau = 0.5 * (lo + hi);
            Vec2State impact{prev.p, prev.v};
            rk4_step(impact, tau, gravitational_parameter);
            result.impacts = true;
            result.impact_in_s = t_prev + tau;
            result.impact_position = impact.p;
            result.impact_speed = speed(impact.v);
            result.arc.push_back(impact.p);
            return result;
        }
        if ((step + 1) % arc_every == 0) {
            result.arc.push_back(state.p);
        }
    }
    return result;  // no surface crossing within max_t
}

}  // namespace aegis::orbit
